#ifdef __wasi__


#include "online/http_request.hpp"
#include "io/file_manager.hpp"
#include "config/user_config.hpp"
#include "online/request_manager.hpp"
#include "utils/file_utils.hpp"
#include "utils/log.hpp"

// ============================================================================
bool Online::globalHTTPRequestInit()
{
    return false;
}   // globalHTTPRequestInit

// ============================================================================
void Online::globalHTTPRequestCleanup()
{
}   // globalHTTPRequestCleanup

// ----------------------------------------------------------------------------
void Online::HTTPRequest::operation()
{
    setProgress(-1.0f);
    Request::operation();
}   // operation

// ----------------------------------------------------------------------------
const char* Online::HTTPRequest::getDownloadErrorMessage() const
{
    return "curl us currently unsupported in wasi";
}   // getDownloadErrorMessage

#endif
