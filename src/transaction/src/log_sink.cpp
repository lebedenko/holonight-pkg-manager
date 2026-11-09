#include "holonight_packages_transaction/log_sink.h"

namespace holonight_packages_transaction {

std::string redactUrlCredentials(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  std::size_t position = 0;
  while (position < text.size()) {
    const std::size_t scheme = text.find("://", position);
    if (scheme == std::string_view::npos) {
      result.append(text.substr(position));
      break;
    }
    const std::size_t authority = scheme + 3;
    result.append(text.substr(position, authority - position));
    // The authority ends at the first '/', '?', '#', whitespace or the end of the text.
    std::size_t end = authority;
    while (end < text.size() && text[end] != '/' && text[end] != '?' && text[end] != '#' && text[end] != ' ' &&
           text[end] != '\t' && text[end] != '\n' && text[end] != '\'' && text[end] != '"') {
      ++end;
    }
    const std::string_view authorityText = text.substr(authority, end - authority);
    if (const std::size_t separator = authorityText.rfind('@'); separator != std::string_view::npos) {
      result.append("***@");
      result.append(authorityText.substr(separator + 1));
    } else {
      result.append(authorityText);
    }
    position = end;
  }
  return result;
}

}  // namespace holonight_packages_transaction
