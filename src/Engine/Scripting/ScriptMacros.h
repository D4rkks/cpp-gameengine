#pragma once

#include "../Scene/Components.h"
#include <functional>

namespace Engine::Internal {

using ScriptFactory = std::function<ScriptableEntity*()>;

void RegisterScriptFactory(const char* name, ScriptFactory factory);

}

#define REGISTER_SCRIPT(ClassName)                                              \
    namespace {                                                                 \
        struct ClassName##_AutoRegistrar {                                      \
            ClassName##_AutoRegistrar() {                                       \
                ::Engine::Internal::RegisterScriptFactory(                      \
                    #ClassName,                                                 \
                    []() -> ::Engine::ScriptableEntity* {                       \
                        return new ClassName();                                 \
                    });                                                         \
            }                                                                   \
        };                                                                      \
        static ClassName##_AutoRegistrar ClassName##_autoreg_instance;          \
    }
