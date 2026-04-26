mod enums;
mod methods;
mod test;

use std::{collections::HashMap, fmt::Display, fs};

use regex::Regex;
use serde::{Deserialize, Deserializer, Serialize, Serializer};
use serde_json::{Value, Map};
use enums::{
  AccessorType, ComponentType, AnimationChannelTargetPath, AnimationSamplerInterpolationType, 
  SamplerFilter, SamplerWrap, MaterialAlphaMode, MeshPrimitiveMode
};
use methods::{
  default_base_color_factor, default_emissive_factor, default_mesh_primitive_mode, 
  default_f32_1, default_f32_half, default_matrix,
  default_rotation, default_scale, default_translation,
  serialize_to_i32, serialize_option_to_i32, serialize_to_str, serialize_option_to_str,
  deserialize_from_i32_to_enum, deserialize_from_option_i32_to_enum,
  deserialize_from_string_to_enum, deserialize_from_option_string_to_enum,
};

use crate::gltf_loader::enums::{BufferViewTarget, CameraType, ImageMimeType};

trait Validatable { fn is_valid(&self) -> Result<(), String>; }

fn check_for_dup_items<T>(v: &Vec<T>, name: &str) -> Result<(), String> 
where T: PartialEq + Ord + Clone
{
  let mut copy = v.clone();
  copy.sort();
  let has_dup = copy.windows(2).any(|items| { items[0] == items[1] });
  if has_dup {
    Err(format!("`{}` must have unique items!", name))?
  }
  Ok(())
}

fn check_if_empty<T>(v: &Vec<T>, name: &str) -> Result<(), String>
{
  if v.is_empty() { Err(format!("`{}` must hold at least one item!", name))? }
  Ok(())
}

fn check_items_for_min_val<T>(v: &Vec<T>, min: T, name: &str) -> Result<(), String>
where T: PartialOrd + Display + Copy
{
  let has_less_than_min = v.iter().any(|&item| item < min); 
  if has_less_than_min { Err(format!("Each item of `{}` must be greater than {}!", name, min))? };
  Ok(())
}

#[derive(Serialize, Deserialize, Debug)]
pub struct GltfLoader {
  // Names of glTF extensions used in this asset.
  #[serde(rename = "extensionsUsed")]
  extensions_used: Option<Vec<String>>,
  // Names of glTF extensions required to properly load this asset.
  #[serde(rename = "extensionsRequired")]
  extensions_required: Option<Vec<String>>,
  // An array of accessors.  An accessor is a typed view into a bufferView.
  accessors: Option<Vec<Accessor>>,
  // An array of keyframe animations.
  animations: Option<Vec<Animation>>,
  // Metadata about the glTF asset.
  asset: Asset,
  // An array of buffers.  A buffer points to binary geometry, animation, or skins.
  buffers: Option<Vec<Buffer>>,
  // An array of bufferViews.  A bufferView is a view into a buffer generally representing a subset of the buffer.
  #[serde(rename = "bufferViews")]
  buffer_views: Option<Vec<BufferView>>,
  // An array of cameras.  A camera defines a projection matrix.
  cameras: Option<Vec<Camera>>,
  // An array of images.  An image defines data used to create a texture.
  images: Option<Vec<Image>>,
  // An array of materials.  A material defines the appearance of a primitive.
  materials: Option<Vec<Material>>,
  // An array of meshes.  A mesh is a set of primitives to be rendered.
  meshes: Option<Vec<Mesh>>,
  // An array of nodes.
  nodes: Option<Vec<Node>>,
  // An array of samplers.
  samplers: Option<Vec<Sampler>>,
  // The index of the default scene.  This property **MUST NOT** be defined, when `scenes` is undefined.
  scene: Option<i32>, // min: 0
  // An array of scenes.
  scenes: Option<Vec<Scene>>,
  // An array of skins.  A skin is defined by joints and matrices.
  skins: Option<Vec<Skin>>,
  // An array of textures.
  textures: Option<Vec<Texture>>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for GltfLoader {
  fn is_valid(&self) -> Result<(), String> {
    if let Some(ext_used)     = &self.extensions_used     { check_if_empty(ext_used,      "extensionsUsed"    )? ; 
                                                            check_for_dup_items(ext_used, "extensionsUsed"    )? }
    if let Some(ext_req)      = &self.extensions_required { check_if_empty(ext_req,       "extensionsRequired")? ; 
                                                            check_for_dup_items(ext_req,  "extensionsRequired")? }
    if let Some(accessors)    = &self.accessors           { check_if_empty(accessors,     "accessors"         )? }
    if let Some(animations)   = &self.animations          { check_if_empty(animations,    "animations"        )? }
    if let Some(buffers)      = &self.buffers             { check_if_empty(buffers,       "buffers"           )? }
    if let Some(buffer_views) = &self.buffer_views        { check_if_empty(buffer_views,  "buffer_views"      )? }
    if let Some(cameras)      = &self.cameras             { check_if_empty(cameras,       "cameras"           )? }
    if let Some(images)       = &self.images              { check_if_empty(images,        "images"            )? }
    if let Some(materials)    = &self.materials           { check_if_empty(materials,     "materials"         )? }
    if let Some(meshes)       = &self.meshes              { check_if_empty(meshes,        "meshes"            )? }
    if let Some(nodes)        = &self.nodes               { check_if_empty(nodes,         "nodes"             )? }
    if let Some(samplers)     = &self.samplers            { check_if_empty(samplers,      "samplers"          )? }
    if let Some(scenes)       = &self.scenes              { check_if_empty(scenes,        "scenes"            )? }
    if let Some(skins)        = &self.skins               { check_if_empty(skins,         "skins"             )? }
    if let Some(textures)     = &self.textures            { check_if_empty(textures,      "textures"          )? }

    if let Some(scene) = &self.scene {
      if self.scenes.is_none()  { Err(format!("`scene` must not be defined, when `scenes` is not defined"))?}
      check_items_for_min_val(Vec::from([scene.clone()]).as_ref(), 0, "scene")?;
    }
    Ok(())
  }
}


// A typed view into a buffer view that contains raw binary data.
#[derive(Serialize, Deserialize, Debug)]
pub struct Accessor {
  // The index of the buffer view. When undefined, the accessor **MUST** be initialized with zeros; 
  // `sparse` property or extensions **MAY** override zeros with actual values.
  #[serde(rename = "bufferView")]
  buffer_view: Option<i32>, // min: 0
  // The offset relative to the start of the buffer view in bytes.
  // This **MUST** be a multiple of the size of the component datatype. 
  // This property **MUST NOT** be defined when `bufferView` is undefined.
  #[serde(rename = "byteOffset", default)]
  byte_offset: Option<i32>, // min: 0, default: 0
  // The datatype of the accessor's components.
  // UNSIGNED_INT type **MUST NOT** be used for any accessor that is not referenced by `mesh.primitive.indices`.
  #[serde(rename = "componentType",
    serialize_with = "serialize_to_i32",
    deserialize_with = "deserialize_from_i32_to_enum",
  )]
  component_type: ComponentType, // Can be any ComponentType
  // Specifies whether integer data values are normalized (`true`) to [0, 1] (for unsigned types) 
  // or to [-1, 1] (for signed types) when they are accessed.
  // This property **MUST NOT** be set to `true` for accessors with `FLOAT` or `UNSIGNED_INT` component type.
  #[serde(default)]
  normalized: bool, // default: false
  // The number of elements referenced by this accessor, 
  // not to be confused with the number of bytes or number of components.
  count: i32, // min: 1
  // Specifies if the accessor's elements are scalars, vectors, or matrices.
  #[serde(rename = "type",
    serialize_with = "serialize_to_str",
    deserialize_with = "deserialize_from_string_to_enum",
  )]
  ty: AccessorType, // anyOf: SCALAR, VEC2, VEC3, VEC4, MAT2, MAT3, MAT4, or some string
  // Maximum value of each component in this accessor.
  // Array elements **MUST** be treated as having the same data type as accessor's `componentType`. 
  // Both `min` and `max` arrays have the same length. 
  // The length is determined by the value of the `type` property; it can be 1, 2, 3, 4, 9, or 16.
  // `normalized` property has no effect on array values: they always correspond to the actual values stored in the buffer. 
  // When the accessor is sparse, this property **MUST** contain maximum values of accessor data with sparse substitution applied.
  max: Option<Vec<f32>>, // min_items: 1, max_items: 16 CHECK MIN MAX AFTER LOAD
  // Minimum value of each component in this accessor.
  // Array elements **MUST** be treated as having the same data type as accessor's `componentType`.
  // Both `min` and `max` arrays have the same length.
  // The length is determined by the value of the `type` property; it can be 1, 2, 3, 4, 9, or 16.
  // `normalized` property has no effect on array values: they always correspond to the actual values stored in the buffer. 
  // When the accessor is sparse, this property **MUST** contain minimum values of accessor data with sparse substitution applied.
  min: Option<Vec<f32>>, // min_items: 1, max_items: 16 CHECK MIN MAX AFTER LOAD
  // Sparse storage of elements that deviate from their initialization value.
  sparse: Option<AccessorSparse>,
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for Accessor {
  fn is_valid(&self) -> Result<(), String> {
    Ok(())
  }
}

// A keyframe animation.
#[derive(Serialize, Deserialize, Debug)]
pub struct Animation {
  // An array of animation channels. An animation channel combines an animation sampler with a target property being animated. 
  // Different channels of the same animation **MUST NOT** have the same targets.
  channels: Vec<AnimationChannel>, // min_items: 1 CHECK MIN ITEMS AFTER LOAD
  // An array of animation samplers. 
  // An animation sampler combines timestamps with a sequence of output values and defines an interpolation algorithm.
  samplers: Vec<AnimationSampler>, // min_items: 1 CHECK MIN ITEMS AFTER LOAD
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for Animation {
  fn is_valid(&self) -> Result<(), String> {
      Ok(())
  }
}

// Metadata about the glTF asset.
#[derive(Serialize, Deserialize, Debug)]
pub struct Asset {
  // A copyright message suitable for display to credit the content creator.
  copyright: Option<String>,
  // Tool that generated this glTF model.  Useful for debugging.
  generator: Option<String>,
  // The glTF version in the form of `<major>.<minor>` that this asset targets.
  version: String, // pattern: ^[0-9]+\\.[0-9]+$
  // The minimum glTF version in the form of `<major>.<minor>` that this asset targets. 
  // This property **MUST NOT** be greater than the asset version.
  #[serde(rename = "minVersion")]
  min_version: Option<String>, // pattern: ^[0-9]+\\.[0-9]+$
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for Asset {
  fn is_valid(&self) -> Result<(), String> {
    let version_regex = Regex::new("^[0-9]+\\.[0-9]+$").unwrap();
    if !version_regex.is_match(&self.version) {
      return Err(format!("`asset.version` must match regex: ^[0-9]+\\.[0-9]+$"))
    }
    if self.min_version.is_some() {
      let min_version = self.min_version.as_ref().unwrap();
      if !version_regex.is_match(min_version) {
        return Err(format!("`asset.version` must match regex: ^[0-9]+\\.[0-9]+$"))
      }
      let version_parts: Vec<&str> = self.version.split(".").collect();
      let min_version_parts: Vec<&str> = min_version.split(".").collect();
      if version_parts.len() == 2 && version_parts.len() == min_version_parts.len() {
        if version_parts[0] >= min_version_parts[0] {
          if version_parts[1] < min_version_parts[1] {
            return Err(format!("`asset.min_version` must be less than or equal to `asset.version`"));
          }
        }
        else {
          return Err(format!("`asset.min_version` must be less than or equal to `asset.version`"));
        }
      }
    }
    Ok(())
  }
}

// A buffer points to binary geometry, animation, or skins.
#[derive(Serialize, Deserialize, Debug)]
pub struct Buffer {
  // The URI (or IRI) of the buffer.  Relative paths are relative to the current glTF asset.
  // Instead of referencing an external file, this field **MAY** contain a `data:`-URI.
  uri: Option<String>, // format: iri-reference, gltf_uriType: application
  // The length of the buffer in bytes.
  #[serde(rename = "byteLength")]
  byte_length: i32, // min: 1
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for Buffer {
  fn is_valid(&self) -> Result<(), String> {
      Ok(())
  }
}

// A view into a buffer generally representing a subset of the buffer.
#[derive(Serialize, Deserialize, Debug)]
pub struct BufferView {
  // The index of the buffer.
  buffer: i32, // min: 0
  // The offset into the buffer in bytes.
  #[serde(rename = "byteOffset")]
  byte_offset: Option<i32>, // min: 0, default: 0
  // The length of the buffer_view in bytes.
  #[serde(rename = "byteLength")]
  byte_length: i32, // min: 1
  // The stride, in bytes, between vertex attributes. 
  // When this is not defined, data is tightly packed. 
  // When two or more accessors use the same buffer view, this field **MUST** be defined.
  #[serde(rename = "byteStride")]
  byte_stride: Option<i32>, // min: 4, max: 252, multipleOf: 4,
  // The hint representing the intended GPU buffer type to use with this buffer view.
  #[serde(
    serialize_with = "serialize_option_to_i32",
    deserialize_with = "deserialize_from_option_i32_to_enum", 
    default
  )]
  target: Option<BufferViewTarget>,
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for BufferView {
  fn is_valid(&self) -> Result<(), String> {
      Ok(())
  }
}

// A camera's projection.  A node **MAY** reference a camera to apply a transform to place the camera in the scene.
#[derive(Serialize, Deserialize, Debug)]
pub struct Camera {
  // An orthographic camera containing properties to create an orthographic projection matrix. 
  // This property **MUST NOT** be defined when `perspective` is defined.
  orthographic: Option<Orthographic>,
  // A perspective camera containing properties to create a perspective projection matrix. 
  // This property **MUST NOT** be defined when `orthographic` is defined.
  perspective: Option<Perspective>,
  // Specifies if the camera uses a perspective or orthographic projection.
  // Based on this, either the camera's `perspective` or `orthographic` property **MUST** be defined.
  #[serde(rename = "type", 
    serialize_with = "serialize_to_str",
    deserialize_with = "deserialize_from_string_to_enum"
  )]
  ty: CameraType, // anyOf: perspective, orthographic, or some string
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for Camera {
  fn is_valid(&self) -> Result<(), String> {
      Ok(())
  }
}

// Image data used to create a texture. Image **MAY** be referenced by an URI (or IRI) or a buffer view index.
#[derive(Serialize, Deserialize, Debug)]
pub struct Image {
  // The URI (or IRI) of the image.  Relative paths are relative to the current glTF asset.  
  // Instead of referencing an external file, this field **MAY** contain a `data:`-URI. 
  // This field **MUST NOT** be defined when `bufferView` is defined.
  uri: Option<String>, // format: iri-reference, gltf_uriType: image
  // The image's media type. This field **MUST** be defined when `bufferView` is defined.
  #[serde(rename = "mimeType",
    serialize_with = "serialize_option_to_str",
    deserialize_with = "deserialize_from_option_string_to_enum",
    default
  )]
  mime_type: Option<ImageMimeType>, // anyOf: image/jpeg, image/png, or some string
  // The index of the bufferView that contains the image. This field **MUST NOT** be defined when `uri` is defined.
  #[serde(rename = "bufferView")]
  buffer_view: Option<i32>, // min: 0
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for Image {
  fn is_valid(&self) -> Result<(), String> {
      Ok(())
  }
}

// The material appearance of a primitive.
#[derive(Serialize, Deserialize, Debug)]
pub struct Material {
  // A set of parameter values that are used to define the metallic-roughness material model from Physically Based Rendering (PBR) methodology. 
  // When undefined, all the default values of `pbrMetallicRoughness` **MUST** apply.
  #[serde(rename = "pbrMetallicRoughness")]
  pbr_metallic_roughness: Option<MaterialPbrMetallicRoughness>,
  // The tangent space normal texture. The texture encodes RGB components with linear transfer function. 
  // Each texel represents the XYZ components of a normal vector in tangent space. 
  // The normal vectors use the convention +X is right and +Y is up. +Z points toward the viewer. 
  // If a fourth component (A) is present, it **MUST** be ignored. When undefined, the material does not have a tangent space normal texture.
  #[serde(rename = "normalTexture")]
  normal_texture: Option<MaterialNormalTextureInfo>,
  // The occlusion texture. The occlusion values are linearly sampled from the R channel. 
  // Higher values indicate areas that receive full indirect lighting and lower values indicate no indirect lighting. 
  // If other channels are present (GBA), they **MUST** be ignored for occlusion calculations. 
  // When undefined, the material does not have an occlusion texture.
  #[serde(rename = "occlusionTexture")]
  occlusion_texture: Option<MaterialOcclusionTextureInfo>,
  // The emissive texture. It controls the color and intensity of the light being emitted by the material. 
  // This texture contains RGB components encoded with the sRGB transfer function. 
  // If a fourth component (A) is present, it **MUST** be ignored. 
  // When undefined, the texture **MUST** be sampled as having `1.0` in RGB components.
  #[serde(rename = "emissiveTexture")]
  emissive_texture: Option<TextureInfo>,
  // The factors for the emissive color of the material. 
  // This value defines linear multipliers for the sampled texels of the emissive texture.
  #[serde(rename = "emissiveFactor", default = "default_emissive_factor")]
  emissive_factor: [f32 /* min: 0.0, max: 1.0 */; 3], // minItems: 3, maxItems: 3, default: [ 0.0, 0.0, 0.0 ]
  // The material's alpha rendering mode enumeration specifying the interpretation of the alpha value of the base color.
  #[serde(rename = "alphaMode", 
    serialize_with = "serialize_to_str",
    deserialize_with = "deserialize_from_string_to_enum",
    default
  )]
  alpha_mode: MaterialAlphaMode, // OPAQUE, MASK, BLEND, or some string. default: OPAQUE
  // Specifies the cutoff threshold when in `MASK` alpha mode. 
  // If the alpha value is greater than or equal to this value then it is rendered as fully opaque, otherwise, it is rendered as fully transparent. 
  // A value greater than `1.0` will render the entire material as fully transparent. 
  // This value **MUST** be ignored for other alpha modes. When `alphaMode` is not defined, this value **MUST NOT** be defined.
  #[serde(rename = "alphaCutoff", default = "default_f32_half")]
  alpha_cutoff: f32, // min: 0.0, default: 0.5
  // Specifies whether the material is double sided. When this value is false, back-face culling is enabled. 
  // When this value is true, back-face culling is disabled and double-sided lighting is enabled. 
  // The back-face **MUST** have its normals reversed before the lighting equation is evaluated.
  #[serde(rename = "doubleSided", default)]
  double_sided: bool, // default: false
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for Material {
  fn is_valid(&self) -> Result<(), String> {
      Ok(())
  }
}

// A set of primitives to be rendered.  Its global transform is defined by a node that references it.
#[derive(Serialize, Deserialize, Debug)]
pub struct Mesh {
  // An array of primitives, each defining geometry to be rendered.
  primitives: Vec<MeshPrimitive>, // minItems: 1
  // Array of weights to be applied to the morph targets. 
  // The number of array elements **MUST** match the number of morph targets.
  weights: Option<Vec<f32>>, // minItems: 1
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for Mesh {
  fn is_valid(&self) -> Result<(), String> {
      Ok(())
  }
}

// A node in the node hierarchy. 
// When the node contains `skin`, all `mesh.primitives` **MUST** contain `JOINTS_0` and `WEIGHTS_0` attributes.
// A node **MAY** have either a `matrix` or any combination of `translation`/`rotation`/`scale` (TRS) properties. 
// TRS properties are converted to matrices and postmultiplied in the `T * R * S` order to compose the transformation matrix; 
// first the scale is applied to the vertices, then the rotation, and then the translation. 
// If none are provided, the transform is the identity. 
// When a node is targeted for animation (referenced by an animation.channel.target), `matrix` **MUST NOT** be present.
//     "not": {
//          "anyOf": [
//              { "required": [ "matrix", "translation" ] },
//              { "required": [ "matrix", "rotation" ] },
//              { "required": [ "matrix", "scale" ] }
//          ]
//      }
#[derive(Serialize, Deserialize, Debug)]
pub struct Node {
  // The index of the camera referenced by this node.
  camera: Option<i32>, // min: 0
  // The indices of this node's children.
  children: Option<Vec<i32 /* min: 0 */>>, // minItems: 1, uniqueItems
  // The index of the skin referenced by this node. 
  // When a skin is referenced by a node within a scene, all joints used by the skin **MUST** belong to the same scene. 
  // When defined, `mesh` **MUST** also be defined.
  skin: Option<i32>, // min: 0
  // A floating-point 4x4 transformation matrix stored in column-major order.
  #[serde(default = "default_matrix")]
  matrix: [f32; 16], // minItems: 16, maxItems: 16, default: [ 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0 ]
  // The index of the mesh in this node.
  mesh: Option<i32>, // min: 0
  // The node's unit quaternion rotation in the order (x, y, z, w), where w is the scalar.
  #[serde(default = "default_rotation")]
  rotation: [f32 /* min: -1.0, max: 1.0 */; 4], // minItems: 4, maxItems: 4, default: [ 0.0, 0.0, 0.0, 1.0 ]
  // The node's non-uniform scale, given as the scaling factors along the x, y, and z axes.
  #[serde(default = "default_scale")]
  scale: [f32; 3], // minItems: 3, maxItems: 3, default: [ 1.0, 1.0, 1.0 ]
  // The node's translation along the x, y, and z axes.
  #[serde(default = "default_translation")]
  translation: [f32; 3], // minItems: 3, maxItems: 3, default: [ 0.0, 0.0, 0.0 ]
  // The weights of the instantiated morph target. 
  // The number of array elements **MUST** match the number of morph targets of the referenced mesh. 
  // When defined, `mesh` **MUST** also be defined.
  weights: Option<Vec<f32>>, // minItems: 1
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for Node {
  fn is_valid(&self) -> Result<(), String> {
      Ok(())
  }
}

// Texture sampler properties for filtering and wrapping modes.
#[derive(Serialize, Deserialize, Debug)]
pub struct Sampler {
  // Magnification filter.
  #[serde(rename = "magFilter",
    serialize_with = "serialize_option_to_i32",
    deserialize_with = "deserialize_from_option_i32_to_enum",
    default
  )]
  mag_filter: Option<SamplerFilter>, // NEAREST, LINEAR, or some integer
  // Minification filter.
  #[serde(rename = "minFilter",
    serialize_with = "serialize_option_to_i32",
    deserialize_with = "deserialize_from_option_i32_to_enum",
    default
  )]
  min_filter: Option<SamplerFilter>, // NEAREST, LINEAR, NEAREST_MIPMAP_NEAREST, LINEAR_MIPMAP_NEAREST, NEAREST_MIPMAP_LINEAR, LINEAR_MIPMAP_LINEAR, or some integer
  // S (U) wrapping mode. All valid values correspond to WebGL enums
  #[serde(rename = "wrapS",
    serialize_with = "serialize_to_i32",
    deserialize_with = "deserialize_from_i32_to_enum",
    default
  )]
  wrap_s: SamplerWrap, // default: REPEAT
  // T (V) wrapping mode.
  #[serde(rename = "wrapT",
    serialize_with = "serialize_to_i32",
    deserialize_with = "deserialize_from_i32_to_enum",
    default
  )]
  wrap_t: SamplerWrap, // default: REPEAT
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for Sampler {
  fn is_valid(&self) -> Result<(), String> {
      Ok(())
  }
}

// The root nodes of a scene.
#[derive(Serialize, Deserialize, Debug)]
pub struct Scene {
  nodes: Option<Vec<i32 /* min: 0 */>>, // minItems: 1, uniqueItems
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for Scene {
  fn is_valid(&self) -> Result<(), String> {
    if self.nodes.is_some() {
      let nodes = self.nodes.as_ref().unwrap();
      if nodes.is_empty() {
        return Err(format!("`scene.nodes` must have at least one item!"));
      }
      let has_neg = nodes.iter().any(|&elem| {elem < 0});
      if has_neg {
        return Err(format!("All elements in `scene.nodes` must be greater than 0"));
      }
      let mut copy: Vec<i32> = nodes.clone();
      copy.sort();
      let has_dup = copy.windows(2).any(|elems| elems[0] == elems[1]);
      if has_dup {
        return Err(format!("`scene.nodes` must have unique items!"));
      }
    }
    Ok(())
  }
}

// Joints and matrices defining a skin.
#[derive(Serialize, Deserialize, Debug)]
pub struct Skin {
  // The index of the accessor containing the floating-point 4x4 inverse-bind matrices. 
  // Its `accessor.count` property **MUST** be greater than or equal to the number of elements of the `joints` array. 
  // When undefined, each matrix is a 4x4 identity matrix.
  #[serde(rename = "inverseBindMatrices")]
  inverse_bind_matrices: Option<i32>, // min: 0
  // The index of the node used as a skeleton root. 
  // The node **MUST** be the closest common root of the joints hierarchy or a direct or indirect parent node of the closest common root.
  skeleton: Option<i32>, // min: 0
  // Indices of skeleton, nodes used as joints in this skin.
  joints: Vec<i32 /* min: 0 */>, // minItems: 1, uniqueItems
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for Skin {
  fn is_valid(&self) -> Result<(), String> {
      Ok(())
  }
}

// A texture and its sampler.
#[derive(Serialize, Deserialize, Debug)]
pub struct Texture {
  // The index of the sampler used by this texture. 
  // When undefined, a sampler with repeat wrapping and auto filtering **SHOULD** be used.
  sampler: Option<i32>, // min: 0
  // The index of the image used by this texture. 
  // When undefined, an extension or other mechanism **SHOULD** supply an alternate texture source, otherwise behavior is undefined.
  source: Option<i32>, // min: 0
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
impl Validatable for Texture {
  fn is_valid(&self) -> Result<(), String> {
      Ok(())
  }
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Extension {
  #[serde(flatten)]
  additional_properties: Map<String, Value>
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Extra {
  #[serde(flatten)]
  additional_properties: Map<String, Value>
}

// An object pointing to a buffer view containing the indices of deviating accessor values. 
// The number of indices is equal to `accessor.sparse.count`. Indices **MUST** strictly increase.
#[derive(Serialize, Deserialize, Debug)]
pub struct AccessorSparseIndices {
  // The index of the buffer view with sparse indices. 
  // The referenced buffer view **MUST NOT** have its `target` or `byteStride` properties defined. 
  // The buffer view and the optional `byteOffset` **MUST** be aligned to the `componentType` byte length.
  #[serde(rename = "bufferView")]
  buffer_view: i32, // min: 0
  // The offset relative to the start of the buffer view in bytes.
  #[serde(rename = "byteOffset", default)]
  byte_offset: i32, // min: 0, default: 0
  // The indices data type.
  #[serde(rename = "componentType",
    serialize_with = "serialize_to_i32",
    deserialize_with = "deserialize_from_i32_to_enum",
  )]
  component_type: ComponentType, // UNSIGNED_BYTE, UNSIGNED_SHORT, or UNSIGNED_INT
  extensions: Option<Extension>,
  extras: Option<Extra>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct AccessorSparseValues {
  // The index of the bufferView with sparse values. 
  // The referenced buffer view **MUST NOT** have its `target` or `byteStride` properties defined.
  #[serde(rename = "bufferView")]
  buffer_view: i32, // min: 0
  // The offset relative to the start of the bufferView in bytes.
  #[serde(rename = "byteOffset", default)]
  byte_offset: i32, // min: 0, default: 0
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
// Sparse storage of accessor values that deviate from their initialization value.
#[derive(Serialize, Deserialize, Debug)]
pub struct AccessorSparse {
  // Number of deviating accessor values stored in the sparse array.
  count: i32, // min: 1
  // An object pointing to a buffer view containing the indices of deviating accessor values. 
  // The number of indices is equal to `count`. Indices **MUST** strictly increase.
  indices: AccessorSparseIndices,
  // An object pointing to a buffer view containing the deviating accessor values.
  values: AccessorSparseValues,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}

// The descriptor of the animated property.
#[derive(Serialize, Deserialize, Debug)]
pub struct AnimationChannelTarget {
  // The index of the node to animate. When undefined, the animated object **MAY** be defined by an extension.
  node: Option<i32>, // min: 0
  // The name of the node's TRS property to animate, or the "weights" of the Morph Targets it instantiates. 
  // For the "translation" property, the values that are provided by the sampler are the translation along the X, Y, and Z axes. 
  // For the "rotation" property, the values are a quaternion in the order (x, y, z, w), where w is the scalar. 
  // For the "scale" property, the values are the scaling factors along the X, Y, and Z axes.
  #[serde(
    serialize_with = "serialize_to_str",
    deserialize_with = "deserialize_from_string_to_enum"
  )]
  path: AnimationChannelTargetPath, // "translation", "rotation", "scale", "weights", ""
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
// An animation channel combines an animation sampler with a target property being animated.
#[derive(Serialize, Deserialize, Debug)]
pub struct AnimationChannel {
  // The index of a sampler in this animation used to compute the value for the target, 
  // e.g., a node's translation, rotation, or scale (TRS).
  sampler: i32, // min: 0
  // The descriptor of the animated property.
  target: AnimationChannelTarget,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
// An animation sampler combines timestamps with a sequence of output values and defines an interpolation algorithm.
#[derive(Serialize, Deserialize, Debug)]
pub struct AnimationSampler {
  // The index of an accessor containing keyframe timestamps. 
  // The accessor **MUST** be of scalar type with floating-point components. 
  // The values represent time in seconds with `time[0] >= 0.0`, and strictly increasing values, i.e., `time[n + 1] > time[n]`.
  input: i32, // min: 0
  #[serde(
    serialize_with = "serialize_to_str",
    deserialize_with = "deserialize_from_string_to_enum",
    default
  )]
  interpolation: AnimationSamplerInterpolationType, // anyOf: LINEAR, STEP, CUBICSPLINE, or some string
  output: i32, // min: 0
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
// An orthographic camera containing properties to create an orthographic projection matrix.
#[derive(Serialize, Deserialize, Debug)]
pub struct Orthographic {
  // The floating-point horizontal magnification of the view. 
  // This value **MUST NOT** be equal to zero. This value **SHOULD NOT** be negative.
  xmag: f32,
  // The floating-point vertical magnification of the view. 
  // This value **MUST NOT** be equal to zero. This value **SHOULD NOT** be negative.
  ymag: f32,
  // The floating-point distance to the far clipping plane. 
  // This value **MUST NOT** be equal to zero. `zfar` **MUST** be greater than `znear`.
  zfar: f32, // exclusiveMin: 0.0
  // The floating-point distance to the near clipping plane.
  znear: f32, // min: 0.0
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
// A perspective camera containing properties to create a perspective projection matrix.
#[derive(Serialize, Deserialize, Debug)]
pub struct Perspective {
  // The floating-point aspect ratio of the field of view. 
  // When undefined, the aspect ratio of the rendering viewport **MUST** be used.
  #[serde(rename = "aspectRatio")]
  aspect_ratio: Option<f32>, // exclusiveMin: 0.0
  // The floating-point vertical field of view in radians. This value **SHOULD** be less than π.
  yfov: f32, // exclusiveMin: 0.0
  // The floating-point distance to the far clipping plane. 
  // When defined, `zfar` **MUST** be greater than `znear`. 
  // If `zfar` is undefined, client implementations **SHOULD** use infinite projection matrix.
  zfar: Option<f32>, // exclusiveMin: 0.0
  // The floating-point distance to the near clipping plane.
  znear: f32, // exclusiveMin: 0.0
  extensions: Option<Extension>,
  extras: Option<Extra>,
}

// Reference to a texture.
#[derive(Serialize, Deserialize, Debug)]
pub struct TextureInfo {
  // The index of the texture.
  index: i32, // min: 0
  // This integer value is used to construct a string in the format `TEXCOORD_<set index>` 
  // which is a reference to a key in `mesh.primitives.attributes` (e.g. a value of `0` corresponds to `TEXCOORD_0`). 
  // A mesh primitive **MUST** have the corresponding texture coordinate attributes for the material to be applicable to it.
  #[serde(rename = "texCoord", default)]
  tex_coord: i32, // min: 0, default: 0
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
// A set of parameter values that are used to define the metallic-roughness material model from Physically-Based Rendering (PBR) methodology.
#[derive(Serialize, Deserialize, Debug)]
pub struct MaterialPbrMetallicRoughness {
  // The factors for the base color of the material. This value defines linear multipliers for the sampled texels of the base color texture.
  #[serde(rename = "baseColorFactor", default = "default_base_color_factor")]
  base_color_factor: [f32 /* min: 0.0, max: 1.0 */; 4], // minItems: 4, maxItems: 4, default: [ 1.0, 1.0, 1.0, 1.0 ]
  // The base color texture. The first three components (RGB) **MUST** be encoded with the sRGB transfer function. 
  // They specify the base color of the material. 
  // If the fourth component (A) is present, it represents the linear alpha coverage of the material. 
  // Otherwise, the alpha coverage is equal to `1.0`. The `material.alphaMode` property specifies how alpha is interpreted. 
  // The stored texels **MUST NOT** be premultiplied. When undefined, the texture **MUST** be sampled as having `1.0` in all components.
  #[serde(rename = "baseColorTexture")]
  base_color_texture: Option<TextureInfo>,
  // The factor for the metalness of the material. 
  // This value defines a linear multiplier for the sampled metalness values of the metallic-roughness texture.
  #[serde(rename = "metallicFactor", default = "default_f32_1")]
  metallic_factor: f32, // min: 0.0, max: 1.0, default: 1.0
  // The factor for the roughness of the material. 
  // This value defines a linear multiplier for the sampled roughness values of the metallic-roughness texture.
  #[serde(rename = "roughnessFactor", default = "default_f32_1")]
  roughness_factor: f32, // min: 0.0, max: 1.0, default: 1.0
  // The metallic-roughness texture. The metalness values are sampled from the B channel. 
  // The roughness values are sampled from the G channel. These values **MUST** be encoded with a linear transfer function. 
  // If other channels are present (R or A), they **MUST** be ignored for metallic-roughness calculations. 
  // When undefined, the texture **MUST** be sampled as having `1.0` in G and B components.
  #[serde(rename = "metallicRoughnessTexture")]
  metallic_roughness_texture: Option<TextureInfo>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
#[derive(Serialize, Deserialize, Debug)]
pub struct MaterialOcclusionTextureInfo {
  // The index of the texture.
  index: i32, // min: 0
  // This integer value is used to construct a string in the format `TEXCOORD_<set index>` 
  // which is a reference to a key in `mesh.primitives.attributes` (e.g. a value of `0` corresponds to `TEXCOORD_0`). 
  // A mesh primitive **MUST** have the corresponding texture coordinate attributes for the material to be applicable to it.
  #[serde(rename = "texCoord", default)]
  tex_coord: i32, // min: 0, default: 0
  // A scalar parameter controlling the amount of occlusion applied. 
  // A value of `0.0` means no occlusion. A value of `1.0` means full occlusion. 
  // This value affects the final occlusion value as: `1.0 + strength * (<sampled occlusion texture value> - 1.0)`.
  #[serde(default = "default_f32_1")]
  strength: f32, // min: 0.0, max: 1.0, default: 1.0
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
#[derive(Serialize, Deserialize, Debug)]
pub struct MaterialNormalTextureInfo {
  // The index of the texture.
  index: i32, // min: 0
  // This integer value is used to construct a string in the format `TEXCOORD_<set index>` 
  // which is a reference to a key in `mesh.primitives.attributes` (e.g. a value of `0` corresponds to `TEXCOORD_0`). 
  // A mesh primitive **MUST** have the corresponding texture coordinate attributes for the material to be applicable to it.
  #[serde(rename = "texCoord")]
  tex_coord: Option<i32>, // min: 0, default: 0
  // The scalar parameter applied to each normal vector of the texture. 
  // This value scales the normal vector in X and Y directions using the formula: 
  // `scaledNormal =  normalize((<sampled normal texture value> * 2.0 - 1.0) * vec3(<normal scale>, <normal scale>, 1.0))`.
  #[serde(default = "default_f32_1")]
  scale: f32, // default: 1.0
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
#[derive(Serialize, Deserialize, Debug)]
pub struct MeshPrimitive {
  // A plain JSON object, where each key corresponds to a mesh attribute semantic 
  // and each value is the index of the accessor containing attribute's data.
  attributes: HashMap<String, i32>,
  // The index of the accessor that contains the vertex indices.
  // When this is undefined, the primitive defines non-indexed geometry.
  // When defined, the accessor **MUST** have `SCALAR` type and an unsigned integer component type.
  indices: Option<i32>, // min: 0
  // The index of the material to apply to this primitive when rendering.
  material: Option<i32>, // min: 0
  // The topology type of primitives to render.
  #[serde(
    serialize_with = "serialize_to_i32",
    deserialize_with = "deserialize_from_i32_to_enum",
    default = "default_mesh_primitive_mode"
  )]
  mode: MeshPrimitiveMode, // default: 4
  // A plain JSON object specifying attributes displacements in a morph target, 
  // where each key corresponds to one of the three supported attribute semantic (`POSITION`, `NORMAL`, or `TANGENT`) 
  // and each value is the index of the accessor containing the attribute displacements' data.
  targets: Option<Vec<HashMap<String, i32>>>, // minItems: 1
  extensions: Option<Extension>,
  extras: Option<Extra>,
}