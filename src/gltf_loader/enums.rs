use serde::{Deserializer, Deserialize, Serializer, Serialize};

#[derive(Debug)]
pub enum ComponentType {
  Byte = 5120, // integer
  UnsignedByte = 5121, // integer
  Short = 5122, // integer
  UnsignedShort = 5123, // integer
  UnsignedInt = 5125, // integer
  Float = 5126, // integer
  Undefined // integer, default
}
impl ComponentType {
  fn from_i32(val: i32) -> Self {
    match val {
      5120 => ComponentType::Byte,
      5121 => ComponentType::UnsignedByte,
      5122 => ComponentType::Short,
      5123 => ComponentType::UnsignedShort,
      5125 => ComponentType::UnsignedInt,
      5126 => ComponentType::Float,
      _ => ComponentType::Undefined
    }
  }


  fn to_i32(&self) -> i32 {
    match self {
      ComponentType::Byte => 5120,
      ComponentType::UnsignedByte => 5121,
      ComponentType::Short => 5122,
      ComponentType::UnsignedShort => 5123,
      ComponentType::UnsignedInt => 5125,
      ComponentType::Float => 5126,
      _ => 5124
    }
  }
}
pub fn deserialize_component_type<'de, D>(deserializer: D) -> Result<ComponentType, D::Error>
where
  D: Deserializer<'de>,
  {
    let val = i32::deserialize(deserializer)?;
    Ok(ComponentType::from_i32(val))
  }

pub fn serialize_component_type<S>(ty: &ComponentType, serializer: S) -> Result<S::Ok, S::Error>
where
  S: Serializer,
  {
    serializer.serialize_i32(ty.to_i32())
  }

#[derive(Debug)]
pub enum AccessorType {
  Scalar,
  Vec2,
  Vec3,
  Vec4,
  Mat2,
  Mat3,
  Mat4,
  Undefined
}
impl AccessorType {
  fn from_str(val: String) -> Self {
    match val.as_str() {
      "SCALAR" => AccessorType::Scalar,
      "VEC2" => AccessorType::Vec2,
      "VEC3" => AccessorType::Vec3,
      "VEC4" => AccessorType::Vec4,
      "MAT2" => AccessorType::Mat2,
      "MAT3" => AccessorType::Mat3,
      "MAT4" => AccessorType::Mat4,
      _ => AccessorType::Undefined
    }
  }


  fn to_str(&self) -> &str {
    match self {
      AccessorType::Scalar => "SCALAR",
      AccessorType::Vec2 => "VEC2",
      AccessorType::Vec3 => "VEC3",
      AccessorType::Vec4 => "VEC4",
      AccessorType::Mat2 => "MAT2",
      AccessorType::Mat3 => "MAT3",
      AccessorType::Mat4 => "MAT4",
      _ => ""
    }
  }
}
pub fn deserialize_accessor_type<'de, D>(deserializer: D) -> Result<AccessorType, D::Error>
where
  D: Deserializer<'de>,
  {
    let val = String::deserialize(deserializer)?;
    Ok(AccessorType::from_str(val))
  }

pub fn serialize_accessor_type<S>(ty: &AccessorType, serializer: S) -> Result<S::Ok, S::Error>
where
  S: Serializer,
  {
    serializer.serialize_str(ty.to_str())
  }
pub enum InterpolationType {
  // The animated values are linearly interpolated between keyframes. 
  // When targeting a rotation, spherical linear interpolation (slerp) **SHOULD** be used to interpolate quaternions. 
  // The number of output elements **MUST** equal the number of input elements.
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
pub enum BufferViewTarget {
  ArrayBuffer = 34962,
  ElementArrayBuffer = 34963,
  Undefined
}
pub enum MaterialAlphaMode {
  // The alpha value is ignored, and the rendered output is fully opaque.
  Opaque,
  // The rendered output is either fully opaque or fully transparent depending on the alpha value and the specified `alphaCutoff` value; 
  // the exact appearance of the edges **MAY** be subject to implementation-specific techniques such as "Alpha-to-Coverage".
  Mask,
  // The alpha value is used to composite the source and destination areas. 
  // The rendered output is combined with the background using the normal painting operation (i.e. the Porter and Duff over operator).
  Blend,
  Undefined
}
// Geometry to be rendered with the given material.
#[derive(Debug)]
pub enum MeshPrimitiveMode {
  Points = 0,
  Lines = 1,
  LineLoop = 2,
  LineStrip = 3,
  Triangles = 4,
  TriangleStrip = 5,
  TriangleFan = 6,
  Undefined
}
impl MeshPrimitiveMode {
  fn from_i32(val: i32) -> Self {
    match val {
      0 => MeshPrimitiveMode::Points,
      1 => MeshPrimitiveMode::Lines,
      2 => MeshPrimitiveMode::LineLoop,
      3 => MeshPrimitiveMode::LineStrip,
      4 => MeshPrimitiveMode::Triangles,
      5 => MeshPrimitiveMode::TriangleStrip,
      6 => MeshPrimitiveMode::TriangleFan,
      _ => MeshPrimitiveMode::Undefined
    }
  }


  fn to_i32(&self) -> i32 {
    match self {
      MeshPrimitiveMode::Points => 0,
      MeshPrimitiveMode::Lines => 1,
      MeshPrimitiveMode::LineLoop => 2,
      MeshPrimitiveMode::LineStrip => 3,
      MeshPrimitiveMode::Triangles => 4,
      MeshPrimitiveMode::TriangleStrip => 5,
      MeshPrimitiveMode::TriangleFan => 6,
      _ => 7
    }
  }
}
pub fn deserialize_mesh_primitive_mode<'de, D>(deserializer: D) -> Result<Option<MeshPrimitiveMode>, D::Error>
where
  D: Deserializer<'de>,
  {
    let val = Option::<i32>::deserialize(deserializer)?;
    Ok(val.map(MeshPrimitiveMode::from_i32))
  }

pub fn serialize_mesh_primitive_mode<S>(mode: &Option<MeshPrimitiveMode>, serializer: S) -> Result<S::Ok, S::Error>
where
  S: Serializer,
  {
    match mode {
      Some(val) => {
        serializer.serialize_i32(val.to_i32())
      },
      _ => serializer.serialize_none()
    }

  }
pub enum SamplerFilter {
  Nearest = 9728,
  Linear = 9729,
  NearestMipmapNearest = 9984,
  LinearMipmapNearest = 9985,
  NearestMipmapLinear = 9986,
  LinearMipmapLinear = 9987,
  Undefined
}
pub enum SamplerWrap {
  ClampToEdge = 33071,
  MirroredRepeat = 33648,
  Repeat = 10497,
  Undefined
}