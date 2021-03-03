#pragma once

#include <string>

std::wstring ConvertToWchar(const std::string& input);
std::string ConvertFromWchar(const std::wstring& input);