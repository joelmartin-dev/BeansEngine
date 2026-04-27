// Allows checking if undefined at deserialized time with generic types
pub trait Undefinable { fn is_undefined(&self) -> bool; }

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum AnimationChannelTargetPath {
  Translation, Rotation, Scale, Weights, Undefined
}
impl From<String> for AnimationChannelTargetPath {
  fn from(val: String) -> Self {
    match val.as_str() {
      "translation" => AnimationChannelTargetPath::Translation,
      "rotation" => AnimationChannelTargetPath::Rotation,
      "scale" => AnimationChannelTargetPath::Scale,
      "weights" => AnimationChannelTargetPath::Weights,
      _ => AnimationChannelTargetPath::Undefined
    }
  }
}
impl From<AnimationChannelTargetPath> for &str {
  fn from(val: AnimationChannelTargetPath) -> Self {
    match val {
      AnimationChannelTargetPath::Translation => "translation",
      AnimationChannelTargetPath::Rotation => "rotation",
      AnimationChannelTargetPath::Scale => "scale",
      AnimationChannelTargetPath::Weights => "weights",
      _ => ""
    }
  }
}
impl Undefinable for AnimationChannelTargetPath {
  fn is_undefined(&self) -> bool {
      *self == AnimationChannelTargetPath::Undefined
  }
}

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum ComponentType {
  Byte = 5120, UnsignedByte = 5121,
  Short = 5122, UnsignedShort = 5123,
  UnsignedInt = 5125, Float = 5126, 
  Undefined
}
impl From<i32> for ComponentType {
  fn from(val: i32) -> Self {
    match val {
      5120 => ComponentType::Byte, 5121 => ComponentType::UnsignedByte,
      5122 => ComponentType::Short, 5123 => ComponentType::UnsignedShort,
      5125 => ComponentType::UnsignedInt, 5126 => ComponentType::Float,
      _ => ComponentType::Undefined
    }
  }
}
impl From<ComponentType> for i32 {
  fn from(val: ComponentType) -> Self {
    match val {
      ComponentType::Byte => 5120, ComponentType::UnsignedByte => 5121,
      ComponentType::Short => 5122, ComponentType::UnsignedShort => 5123,
      ComponentType::UnsignedInt => 5125, ComponentType::Float => 5126,
      _ => 0
    }
  }
}
impl Undefinable for ComponentType {
  fn is_undefined(&self) -> bool {
      *self == ComponentType::Undefined
  }
}

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum AccessorType {
  Vec2, Vec3, Vec4,
  Mat2, Mat3, Mat4,
  Scalar, Undefined
}
impl From<String> for AccessorType {
  fn from(val: String) -> Self {
    match val.as_str() {
      "VEC2" => AccessorType::Vec2, "VEC3" => AccessorType::Vec3, "VEC4" => AccessorType::Vec4,
      "MAT2" => AccessorType::Mat2, "MAT3" => AccessorType::Mat3, "MAT4" => AccessorType::Mat4,
      "SCALAR" => AccessorType::Scalar, _ => AccessorType::Undefined
    }
  }
}
impl From<AccessorType> for &str {
  fn from(val: AccessorType) -> Self {
    match val {
      AccessorType::Vec2 => "VEC2", AccessorType::Vec3 => "VEC3", AccessorType::Vec4 => "VEC4",
      AccessorType::Mat2 => "MAT2", AccessorType::Mat3 => "MAT3", AccessorType::Mat4 => "MAT4",
      AccessorType::Scalar => "SCALAR", _ => ""
    }
  }
}
impl Undefinable for AccessorType {
  fn is_undefined(&self) -> bool {
      *self == AccessorType::Undefined
  }
}

#[derive(Debug, Default, PartialEq, Clone, Copy)]
pub enum AnimationSamplerInterpolationType {
  // The animated values are linearly interpolated between keyframes. 
  // When targeting a rotation, spherical linear interpolation (slerp) **SHOULD** be used to interpolate quaternions. 
  // The number of output elements **MUST** equal the number of input elements.
  #[default]
  Linear,
  // The animated values remain constant to the output of the first keyframe, until the next keyframe. 
  // The number of output elements **MUST** equal the number of input elements.
  Step,
  // The animation's interpolation is computed using a cubic spline with specified tangents. 
  // The number of output elements **MUST** equal three times the number of input elements. 
  // For each input element, the output stores three elements, an in-tangent, a spline vertex, and an out-tangent. 
  // There **MUST** be at least two keyframes when using this interpolation.
  CubicSpline,
  Undefined
}
impl From<String> for AnimationSamplerInterpolationType {
  fn from(val: String) -> Self {
    match val.as_str() {
      "LINEAR" => AnimationSamplerInterpolationType::Linear,
      "STEP" => AnimationSamplerInterpolationType::Step,
      "CUBICSPLINE" => AnimationSamplerInterpolationType::CubicSpline,
      _ => AnimationSamplerInterpolationType::Undefined
    }
  }
}
impl From<AnimationSamplerInterpolationType> for &str {
  fn from(val: AnimationSamplerInterpolationType) -> Self {
    match val {
      AnimationSamplerInterpolationType::Linear => "LINEAR",
      AnimationSamplerInterpolationType::Step => "STEP",
      AnimationSamplerInterpolationType::CubicSpline => "CUBICSPLINE",
      _ => ""
    }
  }
}
impl Undefinable for AnimationSamplerInterpolationType {
  fn is_undefined(&self) -> bool {
      *self == AnimationSamplerInterpolationType::Undefined
  }
}

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum BufferViewTarget {
  ArrayBuffer = 34962, ElementArrayBuffer = 34963, Undefined
}
impl From<i32> for BufferViewTarget {
  fn from(val: i32) -> Self {
    match val {
      34962 => BufferViewTarget::ArrayBuffer,
      34963 => BufferViewTarget::ElementArrayBuffer,
      _ => BufferViewTarget::Undefined
    }
  }
}
impl From<BufferViewTarget> for i32 {
  fn from(val: BufferViewTarget) -> Self {
    match val {
      BufferViewTarget::ArrayBuffer => 34962,
      BufferViewTarget::ElementArrayBuffer => 34963,
      _ => 0
    }
  }
}
impl Undefinable for BufferViewTarget {
  fn is_undefined(&self) -> bool {
      *self == BufferViewTarget::Undefined
  }
}

#[derive(Debug, Default, PartialEq, Clone, Copy)]
pub enum MaterialAlphaMode {
  // The alpha value is ignored, and the rendered output is fully opaque.
  #[default]
  Opaque,
  // The rendered output is either fully opaque or fully transparent depending on the alpha value and the specified `alphaCutoff` value; 
  // the exact appearance of the edges **MAY** be subject to implementation-specific techniques such as "Alpha-to-Coverage".
  Mask,
  // The alpha value is used to composite the source and destination areas. 
  // The rendered output is combined with the background using the normal painting operation (i.e. the Porter and Duff over operator).
  Blend,
  Undefined
}
impl From<String> for MaterialAlphaMode {
  fn from(val: String) -> Self {
    match val.as_str() {
      "OPAQUE" => MaterialAlphaMode::Opaque,
      "MASK" => MaterialAlphaMode::Mask,
      "BLEND" => MaterialAlphaMode::Blend,
      _ => MaterialAlphaMode::Undefined
    }
  }
}
impl From<MaterialAlphaMode> for &str {
  fn from(val: MaterialAlphaMode) -> Self {
    match val {
      MaterialAlphaMode::Opaque => "OPAQUE",
      MaterialAlphaMode::Mask => "MASK",
      MaterialAlphaMode::Blend => "BLEND",
      _ => ""
    }
  }
}
impl Undefinable for MaterialAlphaMode {
  fn is_undefined(&self) -> bool {
      *self == MaterialAlphaMode::Undefined
  }
}

// Geometry to be rendered with the given material.
#[derive(Debug, PartialEq, Clone, Copy)]
pub enum MeshPrimitiveMode {
  Lines = 1, LineLoop = 2, LineStrip = 3,
  Triangles = 4, TriangleStrip = 5, TriangleFan = 6,
  Points = 0, Undefined = 7
}
impl From<i32> for MeshPrimitiveMode {
  fn from(val: i32) -> Self {
    match val {
      1 => MeshPrimitiveMode::Lines, 2 => MeshPrimitiveMode::LineLoop, 3 => MeshPrimitiveMode::LineStrip,
      4 => MeshPrimitiveMode::Triangles, 5 => MeshPrimitiveMode::TriangleStrip, 6 => MeshPrimitiveMode::TriangleFan,
      0 => MeshPrimitiveMode::Points, _ => MeshPrimitiveMode::Undefined
    }
  }
}
impl From<MeshPrimitiveMode> for i32 {
  fn from(val: MeshPrimitiveMode) -> Self {
    match val {
      MeshPrimitiveMode::Lines => 1, MeshPrimitiveMode::LineLoop => 2, MeshPrimitiveMode::LineStrip => 3,
      MeshPrimitiveMode::Triangles => 4, MeshPrimitiveMode::TriangleStrip => 5, MeshPrimitiveMode::TriangleFan => 6,
      MeshPrimitiveMode::Points => 0, _ => 7
    }
  }
}
impl Undefinable for MeshPrimitiveMode {
  fn is_undefined(&self) -> bool {
      *self == MeshPrimitiveMode::Undefined
  }
}

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum SamplerFilter {
  NearestMipmapNearest = 9984, LinearMipmapNearest = 9985,
  NearestMipmapLinear = 9986, LinearMipmapLinear = 9987,
  Nearest = 9728, Linear = 9729, Undefined
}
impl From<i32> for SamplerFilter {
  fn from(val: i32) -> Self {
    match val {
      9984 => SamplerFilter::NearestMipmapNearest,
      9985 => SamplerFilter::LinearMipmapNearest,
      9986 => SamplerFilter::NearestMipmapLinear,
      9987 => SamplerFilter::LinearMipmapLinear,
      9728 => SamplerFilter::Nearest,
      9729 => SamplerFilter::Linear,
      _ => SamplerFilter::Undefined
    }
  }
}
impl From<SamplerFilter> for i32 {
  fn from(val: SamplerFilter) -> Self {
    match val {
      SamplerFilter::NearestMipmapNearest => 9984,
      SamplerFilter::LinearMipmapNearest => 9985,
      SamplerFilter::NearestMipmapLinear => 9986,
      SamplerFilter::LinearMipmapLinear => 9987,
      SamplerFilter::Nearest => 9728,
      SamplerFilter::Linear => 9729,
      _ => 0
    }
  }
}
impl Undefinable for SamplerFilter {
  fn is_undefined(&self) -> bool {
      *self == SamplerFilter::Undefined
  }
}

#[derive(Debug, Default, PartialEq, Clone, Copy)]
pub enum SamplerWrap {
  ClampToEdge = 33071, MirroredRepeat = 33648, #[default] Repeat = 10497, Undefined
}
impl From<i32> for SamplerWrap {
  fn from(val: i32) -> Self {
    match val {
      33071 => SamplerWrap::ClampToEdge,
      33648 => SamplerWrap::MirroredRepeat,
      10497 => SamplerWrap::Repeat,
      _ => SamplerWrap::Undefined
    }
  }
}
impl From<SamplerWrap> for i32 {
  fn from(val: SamplerWrap) -> Self {
    match val {
      SamplerWrap::ClampToEdge => 33071,
      SamplerWrap::MirroredRepeat => 33648,
      SamplerWrap::Repeat => 10497,
      _ => 0
    }
  }
}
impl Undefinable for SamplerWrap {
  fn is_undefined(&self) -> bool {
      *self == SamplerWrap::Undefined
  }
}

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum CameraType {
  Perspective, Orthographic, Undefined
}
impl From<String> for CameraType {
  fn from(val: String) -> Self {
    match val.as_str() {
      "perspective" => CameraType::Perspective,
      "orthographic" => CameraType::Orthographic,
      _ => CameraType::Undefined
    }
  }
}
impl From<CameraType> for &str {
  fn from(val: CameraType) -> Self {
    match val {
      CameraType::Perspective => "perspective",
      CameraType::Orthographic => "orthographic",
      _ => ""
    }
  }
}
impl Undefinable for CameraType {
  fn is_undefined(&self) -> bool {
      *self == CameraType::Undefined
  }
}

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum ImageMimeType {
  Jpeg, Png, Undefined
}
impl From<String> for ImageMimeType {
  fn from(val: String) -> Self {
    match val.as_str() {
      "image/jpeg" => ImageMimeType::Jpeg, "image/png" => ImageMimeType::Png, _ => ImageMimeType::Undefined
    }
  }
}
impl From<ImageMimeType> for &str {
  fn from(val: ImageMimeType) -> Self {
    match val {
      ImageMimeType::Jpeg => "image/jpeg",
      ImageMimeType::Png => "image/png",
      _ => ""
    }
  }
}
impl Undefinable for ImageMimeType {
  fn is_undefined(&self) -> bool {
      *self == ImageMimeType::Undefined
  }
}