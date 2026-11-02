#pragma once

#include "AE_Effect.h"

namespace functional_fx_suite::hosts::ae::plugin
{
    // After Effects command dispatch for the effect returned by hosts::plugin_effect::get()
    PF_Err main(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output,
                void* extra);
} // namespace functional_fx_suite::hosts::ae::plugin
