#pragma once

#include "gmtools/gmtools.hpp"
#include <nlohmann/json_fwd.hpp>

namespace gmtools::detail {

void read_text_provenance(const nlohmann::json& provenance, Trace& trace);
void write_text_provenance(nlohmann::json& header, const Trace& trace);

} // namespace gmtools::detail
