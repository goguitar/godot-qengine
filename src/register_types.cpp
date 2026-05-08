// register_types.cpp – GDExtension entry point.
//
// Registers AudioEffectQEngine and QEngineDetectorNode with Godot's
// class database.  The export symbol name must match entry_symbol in the
// project's .gdextension file.

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "audio_effect_qengine.hpp"
#include "detector_node.hpp"

using namespace godot;

static void initialize_qengine_module(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    ClassDB::register_class<AudioEffectQEngine>();
    ClassDB::register_class<QEngineDetectorNode>();
}

static void uninitialize_qengine_module(ModuleInitializationLevel /*p_level*/)
{
    // Nothing to clean up.
}

extern "C" {

GDExtensionBool GDE_EXPORT godot_qengine_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    const GDExtensionClassLibraryPtr   p_library,
    GDExtensionInitialization*         r_initialization)
{
    GDExtensionBinding::InitObject init_obj(
        p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(initialize_qengine_module);
    init_obj.register_terminator(uninitialize_qengine_module);
    init_obj.set_minimum_library_initialization_level(
        MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}

} // extern "C"
