#include "AEConfig.h"
#include "AE_EffectVers.h"
#include "plugin_version.h"

#ifndef AE_OS_WIN
    #include "AE_General.r"
#endif

resource 'PiPL' (16000) {
    {
        Kind {
            AEEffect
        },
        Name {
            FLARESIM_AE_EFFECT_NAME
        },
        Category {
            FLARESIM_AE_EFFECT_CATEGORY
        },
#ifdef AE_OS_WIN
    #if defined(AE_PROC_INTELx64)
        CodeWin64X86 {"EffectMain"},
    #elif defined(AE_PROC_ARM64)
        CodeWinARM64 {"EffectMain"},
    #endif
#elif defined(AE_OS_MAC)
        CodeMacIntel64 {"EffectMain"},
        CodeMacARM64 {"EffectMain"},
#endif
        AE_PiPL_Version {
            2,
            0
        },
        AE_Effect_Spec_Version {
            PF_PLUG_IN_VERSION,
            PF_PLUG_IN_SUBVERS
        },
        AE_Effect_Version {
            FLARESIM_AE_PIPL_VERSION
        },
        AE_Effect_Info_Flags {
            0
        },
        AE_Effect_Global_OutFlags {
            0x02000000
        },
        AE_Effect_Global_OutFlags_2 {
            0x00001400
        },
        AE_Effect_Match_Name {
            FLARESIM_AE_EFFECT_MATCH_NAME
        },
        AE_Reserved_Info {
            0
        },
        AE_Effect_Support_URL {
            FLARESIM_AE_SUPPORT_URL
        }
    }
};
