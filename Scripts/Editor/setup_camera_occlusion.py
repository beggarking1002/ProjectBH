"""Run in Unreal Editor with walls/roofs selected. Never run during PIE.

Duplicates source material graphs and material-instance parent chains into
/Game/Camera/Occlusion. Imported originals are never edited. Re-running reuses
generated assets. The level is left dirty for the user to inspect and save.
Requires Python Editor Script Plugin and Editor Scripting Utilities.
"""
import hashlib
import unreal

ROOT = "/Game/Camera/Occlusion"
EL = unreal.EditorAssetLibrary
ML = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
CACHE = {}
VERSION = "BHCameraOcclusion_v1"


def node(material, cls, x=0, y=0):
    return ML.create_material_expression(material, cls, x, y)


def wire(source, target, input_name, output=""):
    if not ML.connect_material_expressions(source, output, target, input_name):
        raise RuntimeError("Cannot connect input: " + input_name)


def scalar(material, name, default):
    result = node(material, unreal.MaterialExpressionScalarParameter)
    result.set_editor_property("parameter_name", name)
    result.set_editor_property("default_value", default)
    return result


def vector(material, name, color):
    result = node(material, unreal.MaterialExpressionVectorParameter)
    result.set_editor_property("parameter_name", name)
    result.set_editor_property("default_value", unreal.LinearColor(*color))
    return result


def custom(material, code, input_names, output_type):
    result = node(material, unreal.MaterialExpressionCustom, -300, 400)
    result.set_editor_property("code", code)
    result.set_editor_property("output_type", output_type)
    inputs = []
    for name in input_names:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        inputs.append(item)
    result.set_editor_property("inputs", inputs)
    return result


def generated_path(source):
    suffix = hashlib.sha1(source.get_path_name().encode("utf-8")).hexdigest()[:10]
    return ROOT + "/" + source.get_name() + "_BHOcc_" + suffix


def check_source(source):
    if isinstance(source, unreal.MaterialInstanceConstant):
        check_source(source.get_editor_property("parent"))
        return
    if not isinstance(source, unreal.Material):
        raise RuntimeError("Only Material / MaterialInstanceConstant is supported")
    if source.get_editor_property("material_domain") != unreal.MaterialDomain.MD_SURFACE:
        raise RuntimeError("Only surface materials are supported")
    if source.get_editor_property("use_material_attributes"):
        raise RuntimeError("Material Attributes graph: add the mask inside its attributes manually")
    front = getattr(unreal.MaterialProperty, "MP_FRONT_MATERIAL", None)
    if front is not None and ML.get_material_property_input_node(source, front):
        raise RuntimeError("Substrate graph: use a manually prepared material or whole-mesh hiding")
    if source.get_editor_property("blend_mode") not in (
        unreal.BlendMode.BLEND_OPAQUE, unreal.BlendMode.BLEND_MASKED
    ):
        raise RuntimeError("Only opaque / masked materials are supported")


def prepare_material(source):
    path = source.get_path_name()
    if path.startswith(ROOT + "/"):
        return source
    if path in CACHE:
        return CACHE[path]
    destination = generated_path(source)
    existing = EL.load_asset(destination) if EL.does_asset_exist(destination) else None
    if existing:
        if EL.get_metadata_tag(existing, "BHCameraSetup") != VERSION:
            raise RuntimeError("Incomplete generated asset; inspect/remove it before retrying: " + destination)
        CACHE[path] = existing
        return existing
    if isinstance(source, unreal.MaterialInstanceConstant):
        parent = prepare_material(source.get_editor_property("parent"))
        result = EL.duplicate_asset(path, destination)
        if not result:
            raise RuntimeError("Failed to duplicate " + path)
        ML.set_material_instance_parent(result, parent)
        ML.update_material_instance(result)
    else:
        result = EL.duplicate_asset(path, destination)
        if not result:
            raise RuntimeError("Failed to duplicate " + path)
        was_masked = result.get_editor_property("blend_mode") == unreal.BlendMode.BLEND_MASKED
        original = ML.get_material_property_input_node(result, unreal.MaterialProperty.MP_OPACITY_MASK) if was_masked else None
        original_output = ML.get_material_property_input_node_output_name(result, unreal.MaterialProperty.MP_OPACITY_MASK) if original else ""
        result.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        mask = custom(result, """
float3 delta = Focus - Cam;
float lengthToFocus = max(length(delta), 1.0);
float3 axis = normalize(lerp(delta / lengthToFocus, Forward, Ortho));
float depth = dot(Pos - Cam, axis);
float3 relative = lerp(Pos - Cam, Pos - Focus, Ortho);
float radial = length(relative - axis * dot(relative, axis));
float projectedRadius = Radius * lerp(max(depth / lengthToFocus, 0.0), 1.0, Ortho);
float radius = projectedRadius * saturate(Amount);
// Do not cut geometry behind the player or outside the view segment.
return (depth > 0.0 && depth < dot(delta, axis) && Amount > 0.0001 && radial < radius) ? 0.0 : 1.0;
""", ["Pos", "Cam", "Focus", "Radius", "Amount", "Ortho", "Forward"], unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        wire(node(result, unreal.MaterialExpressionWorldPosition), mask, "Pos")
        wire(vector(result, "BH_OcclusionCamera", (0, 0, 0, 0)), mask, "Cam", "RGB")
        wire(vector(result, "BH_OcclusionFocus", (0, 0, 0, 0)), mask, "Focus", "RGB")
        wire(scalar(result, "BH_OcclusionRadius", 180.0), mask, "Radius")
        wire(scalar(result, "BH_OcclusionAmount", 0.0), mask, "Amount")
        wire(scalar(result, "BH_OcclusionOrtho", 0.0), mask, "Ortho")
        wire(vector(result, "BH_OcclusionForward", (1, 0, 0, 0)), mask, "Forward", "RGB")
        if original:
            product = node(result, unreal.MaterialExpressionMultiply)
            wire(original, product, "A", original_output)
            wire(mask, product, "B")
            mask = product
        # Cutout is for the camera; keep the original shadow silhouette.
        shadow = node(result, unreal.MaterialExpressionShadowReplace)
        wire(mask, shadow, "Default")
        if original:
            wire(original, shadow, "Shadow", original_output)
        else:
            one = node(result, unreal.MaterialExpressionConstant)
            one.set_editor_property("r", 1.0)
            wire(one, shadow, "Shadow")
        if not ML.connect_material_property(shadow, "", unreal.MaterialProperty.MP_OPACITY_MASK):
            raise RuntimeError("Failed to connect opacity mask")
        ML.layout_material_expressions(result)
        ML.recompile_material(result)
    EL.set_metadata_tag(result, "BHCameraSetup", VERSION)
    EL.save_loaded_asset(result)
    CACHE[path] = result
    return result


def prepare_silhouette():
    path = ROOT + "/M_BH_PlayerSilhouette"
    if EL.does_asset_exist(path):
        existing = EL.load_asset(path)
        if EL.get_metadata_tag(existing, "BHCameraSetup") != VERSION:
            raise RuntimeError("Incomplete silhouette asset; inspect/remove before retrying: " + path)
        return existing
    material = TOOLS.create_asset("M_BH_PlayerSilhouette", ROOT, unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError("Failed to create silhouette material")
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    result = custom(material, """
float isPlayer = 1.0 - step(0.5, abs(Stencil.r - PlayerStencil));
float behind = step(SceneDepth.r + 3.0, PlayerDepth.r);
float visible = isPlayer * behind * step(0.001, PlayerDepth.r);
return lerp(Scene.rgb, Tint.rgb, visible * 0.8);
""", ["Scene", "SceneDepth", "PlayerDepth", "Stencil", "PlayerStencil", "Tint"], unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    for name, texture_id in [
        ("Scene", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0),
        ("SceneDepth", unreal.SceneTextureId.PPI_SCENE_DEPTH),
        ("PlayerDepth", unreal.SceneTextureId.PPI_CUSTOM_DEPTH),
        ("Stencil", unreal.SceneTextureId.PPI_CUSTOM_STENCIL),
    ]:
        sample = node(material, unreal.MaterialExpressionSceneTexture)
        sample.set_editor_property("scene_texture_id", texture_id)
        wire(sample, result, name, "Color")
    wire(scalar(material, "BH_PlayerStencil", 253.0), result, "PlayerStencil")
    wire(vector(material, "BH_SilhouetteColor", (0.1, 0.8, 1.0, 1.0)), result, "Tint", "RGB")
    if not ML.connect_material_property(result, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("Failed to connect silhouette")
    ML.layout_material_expressions(material)
    ML.recompile_material(material)
    EL.set_metadata_tag(material, "BHCameraSetup", VERSION)
    EL.save_loaded_asset(material)
    return material


def main():
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = subsystem.get_selected_level_actors()
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        raise RuntimeError("Stop PIE before preparing materials")
    EL.make_directory(ROOT)
    prepare_silhouette()
    converted = 0
    with unreal.ScopedEditorTransaction("Prepare BH camera occluders"):
        for actor in actors:
            if isinstance(actor, unreal.Pawn):
                continue
            for mesh in actor.get_components_by_class(unreal.StaticMeshComponent):
                materials = [mesh.get_material(i) for i in range(mesh.get_num_materials())]
                if not materials or any(m is None for m in materials):
                    continue
                try:
                    for material in materials:
                        check_source(material)
                    replacements = [prepare_material(material) for material in materials]
                except Exception as error:
                    unreal.log_warning("Skipped " + mesh.get_path_name() + ": " + str(error))
                    continue
                mesh.modify()
                for index, material in enumerate(replacements):
                    mesh.set_material(index, material)
                tags = list(mesh.get_editor_property("component_tags"))
                if unreal.Name("CameraOccluder") not in tags:
                    tags.append(unreal.Name("CameraOccluder"))
                mesh.set_editor_property("component_tags", tags)
                converted += 1
    unreal.log("BH Camera: prepared {} mesh components. Inspect and save the level. Enable Custom Depth-Stencil Pass: Enabled with Stencil for the player silhouette.".format(converted))


if __name__ == "__main__":
    main()
