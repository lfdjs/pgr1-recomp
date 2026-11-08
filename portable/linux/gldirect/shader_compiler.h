#pragma once
#include <string>
namespace pgr::gldirect {
// Compile SDK-generated HLSL, preserving intermediate SPIR-V for other host APIs.
// Throws with the diagnostic path on failure; never replaces a shader with a flat color.
std::string CompileHlsl(const std::string& source, bool vertex);
}
