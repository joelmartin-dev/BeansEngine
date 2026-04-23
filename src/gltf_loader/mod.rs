mod methods;
mod test;

use std::{fs};

use serde::{Deserialize, Serialize};
use serde_json::{Value, Map};


#[derive(Serialize, Deserialize, Debug)]
pub struct Extension {
  #[serde(flatten)]
  additionalProperties: Map<String, Value>
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Extra {
  #[serde(flatten)]
  additionalProperties: Map<String, Value>
}
pub enum ComponentType {
  BYTE = 5120, // integer
  UNSIGNED_BYTE = 5121, // integer
  SHORT = 5122, // integer
  UNSIGNED_SHORT = 5123, // integer
  UNSIGNED_INT = 5125, // integer
  FLOAT = 5126, // integer
  UNDEFINED // integer, default
}
pub enum AccessorType {
  SCALAR,
  VEC2,
  VEC3,
  VEC4,
  MAT2,
  MAT3,
  MAT4,
  UNDEFINED
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
  #[serde(rename = "byteOffset")]
  byte_offset: Option<i32>, // min: 0, default: 0
  // The indices data type.
  #[serde(rename = "componentType")]
  component_type: i32, // UNSIGNED_BYTE, UNSIGNED_SHORT, UNSIGNED_INT, or INT
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
  #[serde(rename = "byteOffset")]
  byte_offset: Option<i32>, // min: 0, default: 0
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
  #[serde(rename = "byteOffset")]
  byte_offset: Option<i32>, // min: 0, default: 0
  // The datatype of the accessor's components.
  // UNSIGNED_INT type **MUST NOT** be used for any accessor that is not referenced by `mesh.primitive.indices`.
  #[serde(rename = "componentType")]
  component_type: i32, // Can be any ComponentType
  // Specifies whether integer data values are normalized (`true`) to [0, 1] (for unsigned types) 
  // or to [-1, 1] (for signed types) when they are accessed.
  // This property **MUST NOT** be set to `true` for accessors with `FLOAT` or `UNSIGNED_INT` component type.
  normalized: Option<bool>, // default: false
  // The number of elements referenced by this accessor, 
  // not to be confused with the number of bytes or number of components.
  count: i32, // min: 1
  // Specifies if the accessor's elements are scalars, vectors, or matrices.
  #[serde(rename = "type")]
  ty: String, // anyOf: SCALAR, VEC2, VEC3, VEC4, MAT2, MAT3, MAT4, or some string
  // Maximum value of each component in this accessor.
  // Array elements **MUST** be treated as having the same data type as accessor's `componentType`. 
  // Both `min` and `max` arrays have the same length. 
  // The length is determined by the value of the `type` property; it can be 1, 2, 3, 4, 9, or 16.
  // `normalized` property has no effect on array values: they always correspond to the actual values stored in the buffer. 
  // When the accessor is sparse, this property **MUST** contain maximum values of accessor data with sparse substitution applied.
  max: Option<Vec<f32>>, // min_items: 1, max_items: 16
  // Minimum value of each component in this accessor.
  // Array elements **MUST** be treated as having the same data type as accessor's `componentType`.
  // Both `min` and `max` arrays have the same length.
  // The length is determined by the value of the `type` property; it can be 1, 2, 3, 4, 9, or 16.
  // `normalized` property has no effect on array values: they always correspond to the actual values stored in the buffer. 
  // When the accessor is sparse, this property **MUST** contain minimum values of accessor data with sparse substitution applied.
  min: Option<Vec<f32>>, // min_items: 1, max_items: 16
  // Sparse storage of elements that deviate from their initialization value.
  sparse: Option<AccessorSparse>,
  name: Option<String>,
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
  path: String, // "translation", "rotation", "scale", "weights", ""
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
pub enum InterpolationType {
  // The animated values are linearly interpolated between keyframes. 
  // When targeting a rotation, spherical linear interpolation (slerp) **SHOULD** be used to interpolate quaternions. 
  // The number of output elements **MUST** equal the number of input elements.
  LINEAR,
  // The animated values remain constant to the output of the first keyframe, until the next keyframe. 
  // The number of output elements **MUST** equal the number of input elements.
  STEP,
  // The animation's interpolation is computed using a cubic spline with specified tangents. 
  // The number of output elements **MUST** equal three times the number of input elements. 
  // For each input element, the output stores three elements, an in-tangent, a spline vertex, and an out-tangent. 
  // There **MUST** be at least two keyframes when using this interpolation.
  CUBICSPLINE,
  UNDEFINED
}
// An animation sampler combines timestamps with a sequence of output values and defines an interpolation algorithm.
#[derive(Serialize, Deserialize, Debug)]
pub struct AnimationSampler {
  // The index of an accessor containing keyframe timestamps. 
  // The accessor **MUST** be of scalar type with floating-point components. 
  // The values represent time in seconds with `time[0] >= 0.0`, and strictly increasing values, i.e., `time[n + 1] > time[n]`.
  input: i32, // min: 0
  interpolation: Option<String>, // anyOf: LINEAR, STEP, CUBICSPLINE, or some string
  output: i32, // min: 0
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
// A keyframe animation.
#[derive(Serialize, Deserialize, Debug)]
pub struct Animation {
  // An array of animation channels. An animation channel combines an animation sampler with a target property being animated. 
  // Different channels of the same animation **MUST NOT** have the same targets.
  channels: Vec<AnimationChannel>, // min_items: 1
  // An array of animation samplers. 
  // An animation sampler combines timestamps with a sequence of output values and defines an interpolation algorithm.
  samplers: Vec<AnimationSampler>, // min_items: 1
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
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
pub enum BufferViewTarget {
  ARRAY_BUFFER = 34962,
  ELEMENT_ARRAY_BUFFER = 34963,
  UNDEFINED
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
  target: Option<i32>,
  name: Option<String>,
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
  #[serde(rename = "type")]
  ty: String, // anyOf: perspective, orthographic, or some string
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
// Image data used to create a texture. Image **MAY** be referenced by an URI (or IRI) or a buffer view index.
#[derive(Serialize, Deserialize, Debug)]
pub struct Image {
  // The URI (or IRI) of the image.  Relative paths are relative to the current glTF asset.  
  // Instead of referencing an external file, this field **MAY** contain a `data:`-URI. 
  // This field **MUST NOT** be defined when `bufferView` is defined.
  uri: Option<String>, // format: iri-reference, gltf_uriType: image
  // The image's media type. This field **MUST** be defined when `bufferView` is defined.
  mime_type: Option<String>, // anyOf: image/jpeg, image/png, or some string
  // The index of the bufferView that contains the image. This field **MUST NOT** be defined when `uri` is defined.
  buffer_view: Option<i32>, // min: 0
  name: Option<String>,
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
  tex_coord: Option<i32>, // min: 0, default: 0
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
pub enum MaterialAlphaMode {
  // The alpha value is ignored, and the rendered output is fully opaque.
  OPAQUE,
  // The rendered output is either fully opaque or fully transparent depending on the alpha value and the specified `alphaCutoff` value; 
  // the exact appearance of the edges **MAY** be subject to implementation-specific techniques such as "Alpha-to-Coverage".
  MASK,
  // The alpha value is used to composite the source and destination areas. 
  // The rendered output is combined with the background using the normal painting operation (i.e. the Porter and Duff over operator).
  BLEND,
  UNDEFINED
}
// A set of parameter values that are used to define the metallic-roughness material model from Physically-Based Rendering (PBR) methodology.
#[derive(Serialize, Deserialize, Debug)]
pub struct MaterialPbrMetallicRoughness {
  // The factors for the base color of the material. This value defines linear multipliers for the sampled texels of the base color texture.
  base_color_factor: Option<Vec<f32 /* min: 0.0, max: 1.0 */>>, // minItems: 4, maxItems: 4, default: [ 1.0, 1.0, 1.0, 1.0 ]
  // The base color texture. The first three components (RGB) **MUST** be encoded with the sRGB transfer function. 
  // They specify the base color of the material. 
  // If the fourth component (A) is present, it represents the linear alpha coverage of the material. 
  // Otherwise, the alpha coverage is equal to `1.0`. The `material.alphaMode` property specifies how alpha is interpreted. 
  // The stored texels **MUST NOT** be premultiplied. When undefined, the texture **MUST** be sampled as having `1.0` in all components.
  base_color_texture: Option<TextureInfo>,
  // The factor for the metalness of the material. 
  // This value defines a linear multiplier for the sampled metalness values of the metallic-roughness texture.
  metallic_factor: Option<f32>, // min: 0.0, max: 1.0, default: 1.0
  // The factor for the roughness of the material. 
  // This value defines a linear multiplier for the sampled roughness values of the metallic-roughness texture.
  roughness_factor: Option<f32>, // min: 0.0, max: 1.0, default: 1.0
  // The metallic-roughness texture. The metalness values are sampled from the B channel. 
  // The roughness values are sampled from the G channel. These values **MUST** be encoded with a linear transfer function. 
  // If other channels are present (R or A), they **MUST** be ignored for metallic-roughness calculations. 
  // When undefined, the texture **MUST** be sampled as having `1.0` in G and B components.
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
  tex_coord: Option<i32>, // min: 0, default: 0
  // A scalar parameter controlling the amount of occlusion applied. 
  // A value of `0.0` means no occlusion. A value of `1.0` means full occlusion. 
  // This value affects the final occlusion value as: `1.0 + strength * (<sampled occlusion texture value> - 1.0)`.
  strength: Option<f32>, // min: 0.0, max: 1.0, default: 1.0
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
  tex_coord: Option<i32>, // min: 0, default: 0
  // The scalar parameter applied to each normal vector of the texture. 
  // This value scales the normal vector in X and Y directions using the formula: 
  // `scaledNormal =  normalize((<sampled normal texture value> * 2.0 - 1.0) * vec3(<normal scale>, <normal scale>, 1.0))`.
  scale: Option<f32>, // default: 1.0
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
// The material appearance of a primitive.
#[derive(Serialize, Deserialize, Debug)]
pub struct Material {
  // A set of parameter values that are used to define the metallic-roughness material model from Physically Based Rendering (PBR) methodology. 
  // When undefined, all the default values of `pbrMetallicRoughness` **MUST** apply.
  pbr_metallic_roughness: Option<MaterialPbrMetallicRoughness>,
  // The tangent space normal texture. The texture encodes RGB components with linear transfer function. 
  // Each texel represents the XYZ components of a normal vector in tangent space. 
  // The normal vectors use the convention +X is right and +Y is up. +Z points toward the viewer. 
  // If a fourth component (A) is present, it **MUST** be ignored. When undefined, the material does not have a tangent space normal texture.
  normal_texture: Option<MaterialNormalTextureInfo>,
  // The occlusion texture. The occlusion values are linearly sampled from the R channel. 
  // Higher values indicate areas that receive full indirect lighting and lower values indicate no indirect lighting. 
  // If other channels are present (GBA), they **MUST** be ignored for occlusion calculations. 
  // When undefined, the material does not have an occlusion texture.
  occlusion_texture: Option<MaterialOcclusionTextureInfo>,
  // The emissive texture. It controls the color and intensity of the light being emitted by the material. 
  // This texture contains RGB components encoded with the sRGB transfer function. 
  // If a fourth component (A) is present, it **MUST** be ignored. 
  // When undefined, the texture **MUST** be sampled as having `1.0` in RGB components.
  emissive_texture: Option<TextureInfo>,
  // The factors for the emissive color of the material. 
  // This value defines linear multipliers for the sampled texels of the emissive texture.
  emissive_factor: Option<Vec<f32 /* min: 0.0, max: 1.0 */>>, // minItems: 3, maxItems: 3, default: [ 0.0, 0.0, 0.0 ]
  // The material's alpha rendering mode enumeration specifying the interpretation of the alpha value of the base color.
  alpha_mode: Option<String>, // OPAQUE, MASK, BLEND, or some string. default: OPAQUE
  // Specifies the cutoff threshold when in `MASK` alpha mode. 
  // If the alpha value is greater than or equal to this value then it is rendered as fully opaque, otherwise, it is rendered as fully transparent. 
  // A value greater than `1.0` will render the entire material as fully transparent. 
  // This value **MUST** be ignored for other alpha modes. When `alphaMode` is not defined, this value **MUST NOT** be defined.
  alpha_cutoff: Option<f32>, // min: 0.0, default: 0.5
  // Specifies whether the material is double sided. When this value is false, back-face culling is enabled. 
  // When this value is true, back-face culling is disabled and double-sided lighting is enabled. 
  // The back-face **MUST** have its normals reversed before the lighting equation is evaluated.
  double_sided: Option<bool>, // default: false
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
// Geometry to be rendered with the given material.
pub enum MeshPrimitiveMode {
  POINTS = 0,
  LINES = 1,
  LINE_LOOP = 2,
  LINE_STRIP = 3,
  TRIANGLES = 4,
  TRIANGLE_STRIP = 5,
  TRIANGLE_FAN = 6,
  UNDEFINED
}
#[derive(Serialize, Deserialize, Debug)]
pub struct MeshPrimitive {
  // A plain JSON object, where each key corresponds to a mesh attribute semantic 
  // and each value is the index of the accessor containing attribute's data.
  attributes: Vec<(String, i32)>, // minProperties: 1, REVISIT
  // The index of the accessor that contains the vertex indices.
  // When this is undefined, the primitive defines non-indexed geometry.
  // When defined, the accessor **MUST** have `SCALAR` type and an unsigned integer component type.
  indices: Option<i32>, // min: 0
  // The index of the material to apply to this primitive when rendering.
  material: Option<i32>, // min: 0
  // The topology type of primitives to render.
  mode: Option<i32>, // default: 4
  // A plain JSON object specifying attributes displacements in a morph target, 
  // where each key corresponds to one of the three supported attribute semantic (`POSITION`, `NORMAL`, or `TANGENT`) 
  // and each value is the index of the accessor containing the attribute displacements' data.
  targets: Option<Vec<(String, i32) /* minProperties: 1 */>>, // minItems: 1
  extensions: Option<Extension>,
  extras: Option<Extra>,
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
  camera: Option<Camera>,
  // The indices of this node's children.
  children: Option<Vec<i32 /* min: 0 */>>, // minItems: 1, uniqueItems
  // The index of the skin referenced by this node. 
  // When a skin is referenced by a node within a scene, all joints used by the skin **MUST** belong to the same scene. 
  // When defined, `mesh` **MUST** also be defined.
  skin: Option<i32>, // min: 0
  // A floating-point 4x4 transformation matrix stored in column-major order.
  matrix: Option<Vec<f32>>, // minItems: 16, maxItems: 16, default: [ 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0 ]
  // The index of the mesh in this node.
  mesh: Option<i32>, // min: 0
  // The node's unit quaternion rotation in the order (x, y, z, w), where w is the scalar.
  rotation: Option<Vec<f32 /* min: -1.0f, max: 1.0f */>>, // minItems: 4, maxItems: 4, default: [ 0.0, 0.0, 0.0, 1.0 ]
  // The node's non-unifomr scale, given as the scaling factors along the x, y, and z axes.
  scale: Option<Vec<f32>>, // minItems: 3, maxItems: 3, default: [ 1.0, 1.0, 1.0 ]
  // The node's translation along the x, y, and z axes.
  translation: Option<Vec<f32>>, // minItems: 3, maxItems: 3, default: [ 0.0, 0.0, 0.0 ]
  // The weights of the instantiated morph target. 
  // The number of array elements **MUST** match the number of morph targets of the referenced mesh. 
  // When defined, `mesh` **MUST** also be defined.
  weights: Option<Vec<f32>>, // minItems: 1
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
pub enum SamplerFilter {
  NEAREST = 9728,
  LINEAR = 9729,
  NEAREST_MIPMAP_NEAREST = 9984,
  LINEAR_MIPMAP_NEAREST = 9985,
  NEAREST_MIPMAP_LINEAR = 9986,
  LINEAR_MIPMAP_LINEAR = 9987,
  UNDEFINED
}
pub enum SamplerWrap {
  CLAMP_TO_EDGE = 33071,
  MIRRORED_REPEAT = 33648,
  REPEAT = 10497,
  UNDEFINED
}
// Texture sampler properties for filtering and wrapping modes.
#[derive(Serialize, Deserialize, Debug)]
pub struct Sampler {
  // Magnification filter.
  mag_filter: Option<i32>, // NEAREST, LINEAR, or some integer
  // Minification filter.
  min_filter: Option<i32>, // NEAREST, LINEAR, NEAREST_MIPMAP_NEAREST, LINEAR_MIPMAP_NEAREST, NEAREST_MIPMAP_LINEAR, LINEAR_MIPMAP_LINEAR, or some integer
  // S (U) wrapping mode. All valid values correspond to WebGL enums
  wrap_s: Option<i32>, // default: REPEAT
  // T (V) wrapping mode.
  wrap_t: Option<i32>, // default: REPEAT
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
// The root nodes of a scene.
#[derive(Serialize, Deserialize, Debug)]
pub struct Scene {
  nodes: Option<Vec<i32 /* min: 0 */>>, // minItems: 1, uniqueItems
  name: Option<String>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}
// Joints and matrices defining a skin.
#[derive(Serialize, Deserialize, Debug)]
pub struct Skin {
  // The index of the accessor containing the floating-point 4x4 inverse-bind matrices. 
  // Its `accessor.count` property **MUST** be greater than or equal to the number of elements of the `joints` array. 
  // When undefined, each matrix is a 4x4 identity matrix.
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

#[derive(Serialize, Deserialize, Debug)]
pub struct GltfLoader {
  // Names of glTF extensions used in this asset.
  extensions_used: Option<Vec<String>>,
  // Names of glTF extensions required to properly load this asset.
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
  textures: Option<Vec<Texture>>
}

#[derive(Serialize, Deserialize, Debug)]
pub struct LoadTest {
    // Names of glTF extensions used in this asset.
  extensionsUsed: Option<Vec<String>>,
  // Names of glTF extensions required to properly load this asset.
  extensionsRequired: Option<Vec<String>>,
  // An array of accessors.  An accessor is a typed view into a bufferView.
  accessors: Option<Vec<Map<String, Value>>>,
  // An array of keyframe animations.
  animations: Option<Vec<Map<String, Value>>>,
  // Metadata about the glTF asset.
  asset: Map<String, Value>,
  // An array of buffers.  A buffer points to binary geometry, animation, or skins.
  buffers: Option<Vec<Buffer>>,
  // An array of bufferViews.  A bufferView is a view into a buffer generally representing a subset of the buffer.
  bufferViews: Option<Vec<Map<String, Value>>>,
  // An array of cameras.  A camera defines a projection matrix.
  cameras: Option<Vec<Map<String, Value>>>,
  // An array of images.  An image defines data used to create a texture.
  images: Option<Vec<Map<String, Value>>>,
  // An array of materials.  A material defines the appearance of a primitive.
  materials: Option<Vec<Map<String, Value>>>,
  // An array of meshes.  A mesh is a set of primitives to be rendered.
  meshes: Option<Vec<Map<String, Value>>>,
  // An array of nodes.
  nodes: Option<Vec<Map<String, Value>>>,
  // An array of samplers.
  samplers: Option<Vec<Map<String, Value>>>,
  // The index of the default scene.  This property **MUST NOT** be defined, when `scenes` is undefined.
  scene: Option<i32>, // min: 0
  // An array of scenes.
  scenes: Option<Vec<Map<String, Value>>>,
  // An array of skins.  A skin is defined by joints and matrices.
  skins: Option<Vec<Map<String, Value>>>,
  // An array of textures.
  textures: Option<Vec<Map<String, Value>>>,
  extensions: Option<Extension>,
  extras: Option<Extra>,
}