"""Select Revenant gun geometry imported from an Unreal FBX export.

Run this in Blender's Scripting workspace after importing the character FBX.
It selects vertices weighted to gun_* bones and weapon_l, then enters Edit Mode.
Press P > Selection to separate the selected gun into a new mesh object.
"""

import bpy


def get_character_mesh():
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not meshes:
        raise RuntimeError("No mesh object was found. Import the Revenant FBX first.")
    return max(meshes, key=lambda obj: len(obj.data.vertices))


if bpy.context.mode != "OBJECT":
    bpy.ops.object.mode_set(mode="OBJECT")

mesh_object = get_character_mesh()

for obj in bpy.context.selected_objects:
    obj.select_set(False)
mesh_object.select_set(True)
bpy.context.view_layer.objects.active = mesh_object

# The screenshot shows the weapon geometry is driven by these bone groups.
target_group_indices = {
    group.index
    for group in mesh_object.vertex_groups
    if group.name.startswith("gun_") or group.name == "weapon_l"
}
if not target_group_indices:
    raise RuntimeError(
        "No gun_/weapon_l vertex groups were found on {}.".format(mesh_object.name)
    )

selected_count = 0
for vertex in mesh_object.data.vertices:
    vertex.select = any(
        assignment.group in target_group_indices and assignment.weight > 0.0
        for assignment in vertex.groups
    )
    selected_count += int(vertex.select)

if not selected_count:
    raise RuntimeError("Gun vertex groups exist but do not affect any vertices.")

# Hide armatures temporarily so the selected gun geometry is visible.
for obj in bpy.context.scene.objects:
    if obj.type == "ARMATURE":
        obj.hide_viewport = True

bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_mode(type="VERT")

print(
    "Selected {} gun vertices on '{}'. Use P > Selection to separate the gun.".format(
        selected_count, mesh_object.name
    )
)
