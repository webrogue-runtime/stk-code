//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2006-2015 SuperTuxKart-Team
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include "modes/soccer_world.hpp"

#include "main_loop.hpp"
#include "audio/music_manager.hpp"
#include "audio/sfx_base.hpp"
#include "config/user_config.hpp"
#include "io/file_manager.hpp"
#include "graphics/irr_driver.hpp"
#include "graphics/render_info.hpp"
#include "karts/kart_model.hpp"
#include "karts/kart_properties.hpp"
#include "karts/kart_rewinder.hpp"
#include "karts/rescue_animation.hpp"
#include "karts/controller/local_player_controller.hpp"
#include "karts/controller/network_player_controller.hpp"
#include "network/network_config.hpp"
#include "network/network_string.hpp"
#include "network/protocols/game_events_protocol.hpp"
#include "network/rewind_info.hpp"
#include "network/rewind_manager.hpp"
#include "network/stk_host.hpp"
#include "physics/physics.hpp"
#include "states_screens/race_gui_base.hpp"
#include "tracks/graph.hpp"
#include "tracks/quad.hpp"
#include "tracks/track.hpp"
#include "tracks/track_object_manager.hpp"
#include "tracks/track_sector.hpp"
#include "utils/constants.hpp"

#include <IMeshSceneNode.h>
#include <numeric>
#include <string>
//-----------------------------------------------------------------------------
/** Constructor. Sets up the clock mode etc.
 */
SoccerWorld::SoccerWorld() : WorldWithRank()
{
    if (race_manager->hasTimeTarget())
    {
        WorldStatus::setClockMode(WorldStatus::CLOCK_COUNTDOWN,
            race_manager->getTimeTarget());
    }
    else
    {
        WorldStatus::setClockMode(CLOCK_CHRONO);
    }

    m_frame_count = 0;
    m_use_highscores = false;
    m_red_ai = 0;
    m_blue_ai = 0;
    m_ball_track_sector = NULL;
}   // SoccerWorld

//-----------------------------------------------------------------------------
/** The destructor frees all data structures.
 */
SoccerWorld::~SoccerWorld()
{
    m_goal_sound->deleteSFX();

    delete m_ball_track_sector;
    m_ball_track_sector = NULL;
}   // ~SoccerWorld

//-----------------------------------------------------------------------------
/** Initializes the soccer world. It sets up the data structure
 *  to keep track of points etc. for each kart.
 */
void SoccerWorld::init()
{
    m_kart_team_map.clear();
    m_kart_position_map.clear();
    WorldWithRank::init();
    m_display_rank = false;
    m_goal_timer   = 0;
    m_ball_hitter  = -1;
    m_ball         = NULL;
    m_ball_body    = NULL;
    m_goal_target  = race_manager->getMaxGoal();
    m_goal_sound   = SFXManager::get()->createSoundSource("goal_scored");

    Track *track = Track::getCurrentTrack();
    if (track->hasNavMesh())
    {
        // Init track sector for ball if navmesh is found
        m_ball_track_sector = new TrackSector();
    }

    TrackObjectManager* tom = track->getTrackObjectManager();
    assert(tom);
    PtrVector<TrackObject>& objects = tom->getObjects();
    for (unsigned int i = 0; i < objects.size(); i++)
    {
        TrackObject* obj = objects.get(i);
        if(!obj->isSoccerBall())
            continue;
        m_ball = obj;
        m_ball_body = m_ball->getPhysicalObject()->getBody();
        // Handle one ball only
        break;
    }
    if (!m_ball)
        Log::fatal("SoccerWorld","Ball is missing in soccer field, abort.");

    m_bgd.init(m_ball->getPhysicalObject()->getRadius());
    m_ball_body->setActivationState(DISABLE_DEACTIVATION);

}   // init

//-----------------------------------------------------------------------------
/** Called when a soccer game is restarted.
 */
void SoccerWorld::reset(bool restart)
{
    WorldWithRank::reset(restart);
    if (race_manager->hasTimeTarget())
    {
        WorldStatus::setClockMode(WorldStatus::CLOCK_COUNTDOWN,
            race_manager->getTimeTarget());
    }
    else
    {
        WorldStatus::setClockMode(CLOCK_CHRONO);
    }

    m_count_down_reached_zero = false;
    m_red_scorers.clear();
    m_red_score_times.clear();
    m_blue_scorers.clear();
    m_blue_score_times.clear();
    m_ball_hitter = -1;
    m_red_kdm.clear();
    m_blue_kdm.clear();
    m_ball_heading = 0.0f;
    m_ball_invalid_timer = 0;
    m_goal_transforms.clear();
    m_goal_transforms.resize(m_karts.size());
    if (m_goal_sound != NULL &&
        m_goal_sound->getStatus() == SFXBase::SFX_PLAYING)
    {
        m_goal_sound->stop();
    }

    if (Track::getCurrentTrack()->hasNavMesh())
    {
        m_ball_track_sector->reset();
    }

    m_reset_ball_ticks = -1;
    m_ball->reset();
    m_bgd.reset();
    m_ball->setEnabled(false);

    // Make the player kart in profiling mode up
    // ie make this kart less likely to affect gaming result
    if (UserConfigParams::m_arena_ai_stats)
        getKart(8)->flyUp();

}   // reset

//-----------------------------------------------------------------------------
void SoccerWorld::onGo()
{
    m_ball->setEnabled(true);
    m_ball->reset();
    WorldWithRank::onGo();
}   // onGo

//-----------------------------------------------------------------------------
void SoccerWorld::terminateRace()
{
    const unsigned int kart_amount = getNumKarts();
    for (unsigned int i = 0; i < kart_amount ; i++)
    {
        // Soccer mode use goal for race result, and each goal time is
        // handled by handlePlayerGoalFromServer already
        m_karts[i]->finishedRace(0.0f, true/*from_server*/);
    }   // i<kart_amount
    WorldWithRank::terminateRace();
}   // terminateRace

//-----------------------------------------------------------------------------
/** Returns the internal identifier for this race.
 */
const std::string& SoccerWorld::getIdent() const
{
    return IDENT_SOCCER;
}   // getIdent

//-----------------------------------------------------------------------------
/** Update the world and the track.
 *  \param ticks Physics time steps - should be 1.
 */
void SoccerWorld::update(int ticks)
{
    updateBallPosition(ticks);
    if (Track::getCurrentTrack()->hasNavMesh())
    {
        updateSectorForKarts();
        if (!NetworkConfig::get()->isNetworking())
            updateAIData();
    }

    WorldWithRank::update(ticks);
    WorldWithRank::updateTrack(ticks);

    if (getPhase() == World::GOAL_PHASE)
    {
        for (unsigned int i = 0; i < m_karts.size(); i++)
        {
            auto& kart = m_karts[i];
            if (kart->isEliminated())
                continue;
            kart->getBody()->setLinearVelocity(Vec3(0.0f));
            kart->getBody()->setAngularVelocity(Vec3(0.0f));
            kart->getBody()->proceedToTransform(m_goal_transforms[i]);
            kart->setTrans(m_goal_transforms[i]);
        }

        if (NetworkConfig::get()->isNetworking() &&
            NetworkConfig::get()->isClient())
            return;

        m_goal_timer += ticks;
        if (m_goal_timer > stk_config->time2Ticks(3.0f))
        {
            m_goal_timer = 0;
            if (!isRaceOver())
            {
                // Reset all karts and ball
                resetKartsToSelfGoals();
                if (UserConfigParams::m_arena_ai_stats)
                    getKart(8)->flyUp();
            }
        }
    }
    if (UserConfigParams::m_arena_ai_stats)
        m_frame_count++;

}   // update

//-----------------------------------------------------------------------------
void SoccerWorld::onCheckGoalTriggered(bool first_goal)
{
    if (isRaceOver() || isStartPhase() ||
        (NetworkConfig::get()->isNetworking() &&
        NetworkConfig::get()->isClient()))
        return;

    setPhase(WorldStatus::GOAL_PHASE);
    m_goal_sound->play();
    m_ball->setEnabled(false);
    if (m_ball_hitter != -1)
    {
        if (UserConfigParams::m_arena_ai_stats)
        {
            const int elapsed_frame = m_goal_frame.empty() ? 0 :
                std::accumulate(m_goal_frame.begin(), m_goal_frame.end(), 0);
            m_goal_frame.push_back(m_frame_count - elapsed_frame);
        }

        ScorerData sd;
        sd.m_id = m_ball_hitter;
        sd.m_correct_goal = isCorrectGoal(m_ball_hitter, first_goal);

        if (sd.m_correct_goal)
        {
            m_karts[m_ball_hitter]->getKartModel()
                ->setAnimation(KartModel::AF_WIN_START, true/* play_non_loop*/);
        }

        else if (!sd.m_correct_goal)
        {
            m_karts[m_ball_hitter]->getKartModel()
                ->setAnimation(KartModel::AF_LOSE_START, true/* play_non_loop*/);
        }

        float score_time = 0.0f;
        if (first_goal)
        {
            // Notice: true first_goal means it's blue goal being shoot,
            // so red team can score
            m_red_scorers.push_back(sd);
            if (race_manager->hasTimeTarget())
            {
                score_time = race_manager->getTimeTarget() - getTime();
            }
            else
                score_time = getTime();
            m_red_score_times.push_back(score_time);
        }
        else
        {
            m_blue_scorers.push_back(sd);
            if (race_manager->hasTimeTarget())
            {
                score_time = race_manager->getTimeTarget() - getTime();
            }
            else
                score_time = getTime();
            m_blue_score_times.push_back(score_time);
        }
        if (NetworkConfig::get()->isNetworking() &&
            NetworkConfig::get()->isServer())
        {
            NetworkString p(PROTOCOL_GAME_EVENTS);
            p.setSynchronous(true);
            p.addUInt8(GameEventsProtocol::GE_PLAYER_GOAL)
                .addUInt8((uint8_t)sd.m_id).addUInt8(sd.m_correct_goal)
                .addUInt8(first_goal).addFloat(score_time)
                .addTime(World::getWorld()->getTicksSinceStart() +
                stk_config->time2Ticks(3.0f));
            STKHost::get()->sendPacketToAllPeers(&p, true);
        }
    }
    for (unsigned i = 0; i < m_karts.size(); i++)
    {
        auto& kart = m_karts[i];
        if (kart->isEliminated())
            continue;
        kart->getBody()->setLinearVelocity(Vec3(0.0f));
        kart->getBody()->setAngularVelocity(Vec3(0.0f));
        m_goal_transforms[i] = kart->getBody()->getWorldTransform();
    }

}   // onCheckGoalTriggered

//-----------------------------------------------------------------------------
void SoccerWorld::handleResetBallFromServer(const NetworkString& ns)
{
    int ticks_now = World::getWorld()->getTicksSinceStart();
    int ticks_back_to_own_goal = ns.getTime();
    if (ticks_now >= ticks_back_to_own_goal)
    {
        Log::warn("SoccerWorld", "Server ticks %d is too close to client ticks "
            "%d when reset player", ticks_back_to_own_goal, ticks_now);
        return;
    }
    RewindManager::get()->addRewindInfoEventFunction(new
        RewindInfoEventFunction(ticks_back_to_own_goal,
        [](){}, std::bind(&SoccerWorld::resetKartsToSelfGoals, this)));

}   // handleResetBallFromServer

//-----------------------------------------------------------------------------
void SoccerWorld::handlePlayerGoalFromServer(const NetworkString& ns)
{
    ScorerData sd;
    sd.m_id = ns.getUInt8();
    sd.m_correct_goal = ns.getUInt8() == 1;
    bool first_goal = ns.getUInt8() == 1;
    float score_time = ns.getFloat();
    if (sd.m_correct_goal)
    {
        m_karts[sd.m_id]->getKartModel()
            ->setAnimation(KartModel::AF_WIN_START, true/* play_non_loop*/);
    }
    else if (!sd.m_correct_goal)
    {
        m_karts[sd.m_id]->getKartModel()
            ->setAnimation(KartModel::AF_LOSE_START, true/* play_non_loop*/);
    }

    if (first_goal)
    {
        m_red_scorers.push_back(sd);
        m_red_score_times.push_back(score_time);
    }
    else
    {
        m_blue_scorers.push_back(sd);
        m_blue_score_times.push_back(score_time);
    }
    int ticks_now = World::getWorld()->getTicksSinceStart();
    int ticks_back_to_own_goal = ns.getTime();

    if (ticks_now >= ticks_back_to_own_goal)
    {
        Log::warn("SoccerWorld", "Server ticks %d is too close to client ticks "
            "%d when goal", ticks_back_to_own_goal, ticks_now);
        return;
    }

    setPhase(WorldStatus::GOAL_PHASE);
    m_goal_sound->play();
    m_ball->setEnabled(false);
    for (unsigned i = 0; i < m_karts.size(); i++)
    {
        auto& kart = m_karts[i];
        if (kart->isEliminated())
            continue;
        btTransform transform_now = kart->getBody()->getWorldTransform();
        kart->getBody()->setLinearVelocity(Vec3(0.0f));
        kart->getBody()->setAngularVelocity(Vec3(0.0f));
        kart->getBody()->proceedToTransform(transform_now);
        kart->setTrans(transform_now);
        m_goal_transforms[i] = transform_now;
    }
    RewindManager::get()->addRewindInfoEventFunction(new
        RewindInfoEventFunction(ticks_back_to_own_goal,
        [](){}, std::bind(&SoccerWorld::resetKartsToSelfGoals, this)));

}   // handlePlayerGoalFromServer

//-----------------------------------------------------------------------------
void SoccerWorld::resetKartsToSelfGoals()
{
    m_ball->setEnabled(true);
    m_ball->reset();
    m_bgd.resetCheckGoal(Track::getCurrentTrack());
    setPhase(WorldStatus::RACE_PHASE);
    for (unsigned i = 0; i < m_karts.size(); i++)
    {
        auto& kart = m_karts[i];
        if (kart->isEliminated())
            continue;

        kart->getBody()->setLinearVelocity(Vec3(0.0f));
        kart->getBody()->setAngularVelocity(Vec3(0.0f));
        unsigned index = m_kart_position_map.at(kart->getWorldKartId());
        btTransform t = Track::getCurrentTrack()->getStartTransform(index);
        moveKartTo(kart.get(), t);
    }
}   // resetKartsToSelfGoals

//-----------------------------------------------------------------------------
/** Sets the last kart that hit the ball, to be able to
 * identify the scorer later.
 */
void SoccerWorld::setBallHitter(unsigned int kart_id)
{
    m_ball_hitter = kart_id;
}   // setBallHitter

//-----------------------------------------------------------------------------
/** The soccer game is over if time up or either team wins.
 */
bool SoccerWorld::isRaceOver()
{
    if (m_unfair_team)
        return true;

    if (race_manager->hasTimeTarget())
    {
        return m_count_down_reached_zero;
    }
    // One team scored the target goals ...
    else
    {
        return (getScore(KART_TEAM_BLUE) >= m_goal_target ||
            getScore(KART_TEAM_RED) >= m_goal_target);
    }

}   // isRaceOver

//-----------------------------------------------------------------------------
/** Called when the match time ends.
 */
void SoccerWorld::countdownReachedZero()
{
    // Prevent negative time in network soccer when finishing
    m_time_ticks = 0;
    m_time = 0.0f;
    m_count_down_reached_zero = true;
}   // countdownReachedZero

//-----------------------------------------------------------------------------
bool SoccerWorld::getKartSoccerResult(unsigned int kart_id) const
{
    if (m_red_scorers.size() == m_blue_scorers.size()) return true;

    bool red_win = m_red_scorers.size() > m_blue_scorers.size();
    KartTeam team = getKartTeam(kart_id);

    if ((red_win && team == KART_TEAM_RED) ||
        (!red_win && team == KART_TEAM_BLUE))
        return true;
    else
        return false;

}   // getKartSoccerResult

//-----------------------------------------------------------------------------
/** Localize the ball on the navigation mesh.
 */
void SoccerWorld::updateBallPosition(int ticks)
{
    if (isRaceOver()) return;

    if (!ballNotMoving())
    {
        // Only update heading if the ball is moving
        m_ball_heading = atan2f(m_ball_body->getLinearVelocity().getX(),
            m_ball_body->getLinearVelocity().getZ());
    }

    if (Track::getCurrentTrack()->hasNavMesh())
    {
        m_ball_track_sector
            ->update(getBallPosition(), true/*ignore_vertical*/);
        if (!m_ball_track_sector->isOnRoad() && getPhase() == RACE_PHASE &&
            m_reset_ball_ticks == -1)
        {
            m_ball_invalid_timer += ticks;
            // Reset the ball and karts if out of navmesh after 2 seconds
            if (m_ball_invalid_timer >= stk_config->time2Ticks(2.0f))
            {
                if (NetworkConfig::get()->isNetworking() &&
                    NetworkConfig::get()->isServer())
                {
                    // Reset the ball 2 seconds in the future to make sure it's
                    // after all clients time
                    m_reset_ball_ticks =
                        World::getWorld()->getTicksSinceStart() +
                        stk_config->time2Ticks(2.0f);

                    NetworkString p(PROTOCOL_GAME_EVENTS);
                    p.setSynchronous(true);
                    p.addUInt8(GameEventsProtocol::GE_RESET_BALL)
                        .addTime(m_reset_ball_ticks);
                    STKHost::get()->sendPacketToAllPeers(&p, true);
                }
                else if (!NetworkConfig::get()->isNetworking())
                {
                    m_ball_invalid_timer = 0;
                    resetKartsToSelfGoals();
                    if (UserConfigParams::m_arena_ai_stats)
                        getKart(8)->flyUp();
                }
            }
        }
        else
            m_ball_invalid_timer = 0;
        if (m_reset_ball_ticks == World::getWorld()->getTicksSinceStart())
        {
            assert(NetworkConfig::get()->isNetworking() &&
                NetworkConfig::get()->isServer());
            resetKartsToSelfGoals();
            m_reset_ball_ticks = -1;
        }
    }

}   // updateBallPosition

//-----------------------------------------------------------------------------
int SoccerWorld::getBallNode() const
{
    assert(m_ball_track_sector != NULL);
    return m_ball_track_sector->getCurrentGraphNode();
}   // getBallNode

//-----------------------------------------------------------------------------
bool SoccerWorld::isCorrectGoal(unsigned int kart_id, bool first_goal) const
{
    KartTeam team = getKartTeam(kart_id);
    if (first_goal)
    {
        if (team == KART_TEAM_RED)
            return true;
    }
    else if (!first_goal)
    {
        if (team == KART_TEAM_BLUE)
            return true;
    }
    return false;
}   // isCorrectGoal

//-----------------------------------------------------------------------------
void SoccerWorld::updateAIData()
{
    if (isRaceOver()) return;

    // Fill the kart distance map
    m_red_kdm.clear();
    m_blue_kdm.clear();

    for (unsigned int i = 0; i < m_karts.size(); ++i)
    {
        if (UserConfigParams::m_arena_ai_stats &&
            m_karts[i]->getController()->isPlayerController())
            continue;

        if (getKartTeam(m_karts[i]->getWorldKartId()) == KART_TEAM_RED)
        {
            Vec3 rd = m_karts[i]->getXYZ() - getBallPosition();
            m_red_kdm.push_back(KartDistanceMap(i, rd.length_2d()));
        }
        else
        {
            Vec3 bd = m_karts[i]->getXYZ() - getBallPosition();
            m_blue_kdm.push_back(KartDistanceMap(i, bd.length_2d()));
        }
    }
    // Sort the vectors, so first vector will have the min distance
    std::sort(m_red_kdm.begin(), m_red_kdm.end());
    std::sort(m_blue_kdm.begin(), m_blue_kdm.end());

    // Fill Ball and goals data
    m_bgd.updateBallAndGoal(getBallPosition(), getBallHeading());

}   // updateAIData

//-----------------------------------------------------------------------------
int SoccerWorld::getAttacker(KartTeam team) const
{
    if (team == KART_TEAM_BLUE && m_blue_kdm.size() > 1)
    {
        for (unsigned int i = 1; i < m_blue_kdm.size(); i++)
        {
            // Only AI will do the attack job
            if (getKart(m_blue_kdm[i].m_kart_id)
                ->getController()->isPlayerController())
                continue;
            return m_blue_kdm[i].m_kart_id;
        }
    }
    else if (team == KART_TEAM_RED && m_red_kdm.size() > 1)
    {
        for (unsigned int i = 1; i < m_red_kdm.size(); i++)
        {
            if (getKart(m_red_kdm[i].m_kart_id)
                ->getController()->isPlayerController())
                continue;
            return m_red_kdm[i].m_kart_id;
        }
    }

    // No attacker
    return -1;
}   // getAttacker

//-----------------------------------------------------------------------------
unsigned int SoccerWorld::getRescuePositionIndex(AbstractKart *kart)
{
    if (!Track::getCurrentTrack()->hasNavMesh())
        return m_kart_position_map.at(kart->getWorldKartId());

    int last_valid_node =
        getTrackSector(kart->getWorldKartId())->getLastValidGraphNode();
    if (last_valid_node >= 0)
        return last_valid_node;
    Log::warn("SoccerWorld", "Missing last valid node for rescuing");
    return 0;
}   // getRescuePositionIndex

//-----------------------------------------------------------------------------
btTransform SoccerWorld::getRescueTransform(unsigned int rescue_pos) const
{
    if (!Track::getCurrentTrack()->hasNavMesh())
        return WorldWithRank::getRescueTransform(rescue_pos);

    const Vec3 &xyz = Graph::get()->getQuad(rescue_pos)->getCenter();
    const Vec3 &normal = Graph::get()->getQuad(rescue_pos)->getNormal();
    btTransform pos;
    pos.setOrigin(xyz);
    btQuaternion q1 = shortestArcQuat(Vec3(0.0f, 1.0f, 0.0f), normal);
    pos.setRotation(q1);
    return pos;
}   // getRescueTransform

//-----------------------------------------------------------------------------
void SoccerWorld::enterRaceOverState()
{
    WorldWithRank::enterRaceOverState();

    if (UserConfigParams::m_arena_ai_stats)
    {
        Log::verbose("Soccer AI profiling", "Total frames elapsed for a team"
            " to win with 30 goals: %d", m_frame_count);

        // Goal time statistics
        std::sort(m_goal_frame.begin(), m_goal_frame.end());

        const int mean = std::accumulate(m_goal_frame.begin(),
            m_goal_frame.end(), 0) / (int)m_goal_frame.size();

        // Prevent overflow if there is a large frame in vector
        double squared_sum = 0;
        for (const int &i : m_goal_frame)
            squared_sum = squared_sum + (double(i - mean) * double(i - mean));

        // Use sample st. deviation (n−1) as the profiling can't be run forever
        const int stdev = int(sqrt(squared_sum / (m_goal_frame.size() - 1)));

        int median = 0;
        if (m_goal_frame.size() % 2 == 0)
        {
            median = (m_goal_frame[m_goal_frame.size() / 2 - 1] +
                m_goal_frame[m_goal_frame.size() / 2]) / 2;
        }
        else
        {
            median = m_goal_frame[m_goal_frame.size() / 2];
        }

        Log::verbose("Soccer AI profiling", "Frames elapsed for each goal:"
            " min: %d max: %d mean: %d median: %d standard deviation: %d",
            m_goal_frame.front(), m_goal_frame.back(), mean, median, stdev);

        // Goal calculation
        int red_own_goal = 0;
        int blue_own_goal = 0;
        for (unsigned i = 0; i < m_red_scorers.size(); i++)
        {
            // Notice: if a team has own goal, the score will end up in the
            // opposite team
            if (!m_red_scorers[i].m_correct_goal)
                blue_own_goal++;
        }
        for (unsigned i = 0; i < m_blue_scorers.size(); i++)
        {
            if (!m_blue_scorers[i].m_correct_goal)
                red_own_goal++;
        }

        int red_goal = ((int(m_red_scorers.size()) - blue_own_goal) >= 0 ?
            (int)m_red_scorers.size() - blue_own_goal : 0);
        int blue_goal = ((int(m_blue_scorers.size()) - red_own_goal) >= 0 ?
            (int)m_blue_scorers.size() - red_own_goal : 0);

        Log::verbose("Soccer AI profiling", "Red goal: %d, Red own goal: %d,"
            "Blue goal: %d, Blue own goal: %d", red_goal, red_own_goal,
            blue_goal, blue_own_goal);

        if (getScore(KART_TEAM_BLUE) >= m_goal_target)
            Log::verbose("Soccer AI profiling", "Blue team wins");
        else
            Log::verbose("Soccer AI profiling", "Red team wins");

        delete this;
        main_loop->abort();
    }

}   // enterRaceOverState
