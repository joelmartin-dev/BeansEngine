pub struct Extension {}
pub struct Extra {}
pub enum ComponentType {
  BYTE = 5120, // integer
  UNSIGNED_BYTE = 5121, // integer
  SHORT = 5122, // integer
  UNSIGNED_SHORT = 5123, // integer
  UNSIGNED_INT = 5125, // integer
  FLOAT = 5126, // integer
  DEFAULT // integer, default
}
pub enum AccessorType {
  SCALAR,
  VEC2,
  VEC3,
  VEC4,
  MAT2,
  MAT3,
  MAT4,
  DEFAULT
}
// An object pointing to a buffer view containing the indices of deviating accessor values. 
// The number of indices is equal to `accessor.sparse.count`. Indices **MUST** strictly increase.
pub struct AccessorSparseIndices {
  // The index of the buffer view with sparse indices. 
  // The referenced buffer view **MUST NOT** have its `target` or `byteStride` properties defined. 
  // The buffer view and the optional `byteOffset` **MUST** be aligned to the `componentType` byte length.
  buffer_view: i32, // min: 0
  // The offset relative to the start of the buffer view in bytes.
  byte_offset: Option<i32>, // min: 0, default: 0
  // The indices data type.
  component_type: ComponentType, // UNSIGNED_BYTE, UNSIGNED_SHORT, UNSIGNED_INT, or INT
  extensions: Option<Vec<Extension>>,
  extras: Option<Vec<Extra>>,
}

pub struct AccessorSparseValues {
  // The index of the bufferView with sparse values. 
  // The referenced buffer view **MUST NOT** have its `target` or `byteStride` properties defined.
  buffer_view: i32, // min: 0
  // The offset relative to the start of the bufferView in bytes.
  byte_offset: Option<i32>, // min: 0, default: 0
  extensions: Option<Vec<Extension>>,
  extras: Option<Vec<Extra>>,
}
// Sparse storage of accessor values that deviate from their initialization value.
pub struct AccessorSparse {
  // Number of deviating accessor values stored in the sparse array.
  count: i32, // min: 1
  // An object pointing to a buffer view containing the indices of deviating accessor values. 
  // The number of indices is equal to `count`. Indices **MUST** strictly increase.
  indices: AccessorSparseIndices,
  // An object pointing to a buffer view containing the deviating accessor values.
  values: AccessorSparseValues,
  extensions: Option<Vec<Extension>>,
  extras: Option<Vec<Extra>>,
}
// A typed view into a buffer view that contains raw binary data.
pub struct Accessor {
  // The index of the buffer view. When undefined, the accessor **MUST** be initialized with zeros; 
  // `sparse` property or extensions **MAY** override zeros with actual values.
  buffer_view: Option<i32>, // min: 0
  // The offset relative to the start of the buffer view in bytes.
  // This **MUST** be a multiple of the size of the component datatype. 
  // This property **MUST NOT** be defined when `bufferView` is undefined.
  byte_offset: Option<i32>, // min: 0, default: 0
  // The datatype of the accessor's components.
  // UNSIGNED_INT type **MUST NOT** be used for any accessor that is not referenced by `mesh.primitive.indices`.
  component_type: ComponentType, // Can be any ComponentType
  // Specifies whether integer data values are normalized (`true`) to [0, 1] (for unsigned types) 
  // or to [-1, 1] (for signed types) when they are accessed.
  // This property **MUST NOT** be set to `true` for accessors with `FLOAT` or `UNSIGNED_INT` component type.
  normalized: Option<bool>, // default: false
  // The number of elements referenced by this accessor, 
  // not to be confused with the number of bytes or number of components.
  count: i32, // min: 1
  // Specifies if the accessor's elements are scalars, vectors, or matrices.
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
  extensions: Vec<Extension>,
  extras: Vec<Extra>,
}
// The descriptor of the animated property.
pub struct AnimationChannelTarget {
  // The index of the node to animate. When undefined, the animated object **MAY** be defined by an extension.
  node: Option<i32>, // min: 0
  // The name of the node's TRS property to animate, or the "weights" of the Morph Targets it instantiates. 
  // For the "translation" property, the values that are provided by the sampler are the translation along the X, Y, and Z axes. 
  // For the "rotation" property, the values are a quaternion in the order (x, y, z, w), where w is the scalar. 
  // For the "scale" property, the values are the scaling factors along the X, Y, and Z axes.
  path: String, // "translation", "rotation", "scale", "weights", ""
  extensions: Vec<Extension>,
  extras: Vec<Extra>,
}
// An animation channel combines an animation sampler with a target property being animated.
pub struct AnimationChannel {
  // The index of a sampler in this animation used to compute the value for the target, 
  // e.g., a node's translation, rotation, or scale (TRS).
  sampler: i32, // min: 0
  // The descriptor of the animated property.
  target: AnimationChannelTarget,
  extensions: Vec<Extension>,
  extras: Vec<Extra>,
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
  DEFAULT
}
// An animation sampler combines timestamps with a sequence of output values and defines an interpolation algorithm.
pub struct AnimationSampler {
  // The index of an accessor containing keyframe timestamps. 
  // The accessor **MUST** be of scalar type with floating-point components. 
  // The values represent time in seconds with `time[0] >= 0.0`, and strictly increasing values, i.e., `time[n + 1] > time[n]`.
  input: i32, // min: 0
  interpolation: Option<String>, // anyOf: LINEAR, STEP, CUBICSPLINE, or some string
  output: i32, // min: 0
  extensions: Vec<Extension>,
  extras: Vec<Extra>,
}
// A keyframe animation.
pub struct Animation {
  // An array of animation channels. An animation channel combines an animation sampler with a target property being animated. 
  // Different channels of the same animation **MUST NOT** have the same targets.
  channels: Vec<AnimationChannel>, // min_items: 1
  // An array of animation samplers. 
  // An animation sampler combines timestamps with a sequence of output values and defines an interpolation algorithm.
  samplers: Vec<AnimationSampler>, // min_items: 1
  name: Option<String>,
  extensions: Vec<Extension>,
  extras: Vec<Extra>,
}
// Metadata about the glTF asset.
pub struct Asset {
  // A copyright message suitable for display to credit the content creator.
  copyright: Option<String>,
  // Tool that generated this glTF model.  Useful for debugging.
  generator: Option<String>,
  // The glTF version in the form of `<major>.<minor>` that this asset targets.
  version: String, // pattern: ^[0-9]+\\.[0-9]+$
  // The minimum glTF version in the form of `<major>.<minor>` that this asset targets. 
  // This property **MUST NOT** be greater than the asset version.
  min_version: Option<String>, // pattern: ^[0-9]+\\.[0-9]+$
  extensions: Vec<Extension>,
  extras: Vec<Extra>,
}
// A buffer points to binary geometry, animation, or skins.
pub struct Buffer {
  // The URI (or IRI) of the buffer.  Relative paths are relative to the current glTF asset.
  // Instead of referencing an external file, this field **MAY** contain a `data:`-URI.
  uri: Option<String>, // format: iri-reference, gltf_uriType: application
  // The length of the buffer in bytes.
  byte_length: i32, // min: 1
  name: Option<String>,
  extensions: Vec<Extension>,
  extras: Vec<Extra>,
}
pub enum BufferViewTarget {
  ARRAY_BUFFER = 34962,
  ELEMENT_ARRAY_BUFFER = 34963,
  DEFAULT
}
// A view into a buffer generally representing a subset of the buffer.
pub struct BufferView {
  // The index of the buffer.
  buffer: i32, // min: 0
  // The offset into the buffer in bytes.
  byte_offset: Option<i32>, // min: 0, default: 0
  // The length of the buffer_view in bytes.
  byte_length: i32, // min: 1
  // The stride, in bytes, between vertex attributes. 
  // When this is not defined, data is tightly packed. 
  // When two or more accessors use the same buffer view, this field **MUST** be defined.
  byte_stride: Option<i32>, // min: 4, max: 252, multipleOf: 4,
  // The hint representing the intended GPU buffer type to use with this buffer view.
  target: Option<BufferViewTarget>,
  name: Option<String>,
  extensions: Vec<Extension>,
  extras: Vec<Extra>,
}
pub struct Camera {}
pub struct Image {}
pub struct Material {}
pub struct Mesh {}
pub struct Node {}
pub struct Sampler {}
pub struct Scene {}
pub struct Skin {}
pub struct Texture {}


pub struct GltfLoader {
  // Names of glTF extensions used in this asset.
  extensions_used: Option<Vec<Extension>>,
  // Names of glTF extensions required to properly load this asset.
  extensions_required: Option<Vec<Extension>>,
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