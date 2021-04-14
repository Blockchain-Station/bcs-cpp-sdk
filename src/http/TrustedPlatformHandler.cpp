#include "enjinsdk/TrustedPlatformHandler.hpp"

#include "enjinsdk_utils/StringUtils.hpp"

namespace enjin::sdk::http {

bool TrustedPlatformHandler::is_authenticated() const {
    std::lock_guard<std::mutex> guard(auth_token_mutex);
    return auth_token.has_value() && !utils::is_empty_or_whitespace(auth_token.value());
}

void TrustedPlatformHandler::set_auth_token(const std::string& auth_token) {
    std::lock_guard<std::mutex> guard(auth_token_mutex);
    TrustedPlatformHandler::auth_token = auth_token;
}

const std::optional<std::string>& TrustedPlatformHandler::get_auth_token() const {
    std::lock_guard<std::mutex> guard(auth_token_mutex);
    return auth_token;
}

}
