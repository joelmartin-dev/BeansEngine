use std::{any::type_name, fs, path::PathBuf};

use regex::Regex;
use serde::{Deserialize, Deserializer, Serialize, Serializer, de::Error};

use crate::gltf_loader::{GltfLoader, enums::{
  AccessorType, AnimationChannelTargetPath, AnimationSamplerInterpolationType, BufferViewTarget, CameraType, ComponentType, ImageMimeType, MaterialAlphaMode, MeshPrimitiveMode, SamplerFilter, SamplerWrap, Undefinable
}};

impl GltfLoader {
  pub fn load(path: &PathBuf) -> Result<Self, String> {
    let parsed: Result<GltfLoader, serde_json::Error> = serde_json::from_str(&fs::read_to_string(path).unwrap());

    if parsed.is_ok() 
    {
      let loaded = parsed.as_ref().unwrap();
      // Root Validation
      {
        if loaded.scene.is_some() {
          if loaded.scenes.is_none() {
            return Err(format!("`scene` must not be defined, when `scenes` is not defined!"));
          }
          if *(loaded.scene.as_ref().unwrap()) < 0 {
            return Err(format!("`scene` must be greater than or equal to zero!"));
          }
        }
      }

      // Asset Validation
      {
        let asset = &loaded.asset;
        let version_regex = Regex::new("^[0-9]+\\.[0-9]+$").unwrap();
        if !version_regex.is_match(&asset.version) {
          return Err(format!("`asset.version` must match regex: ^[0-9]+\\.[0-9]+$"))
        }
        if asset.min_version.is_some() {
          let min_version = asset.min_version.as_ref().unwrap();
          if !version_regex.is_match(min_version) {
            return Err(format!("`asset.version` must match regex: ^[0-9]+\\.[0-9]+$"))
          }
          let version_parts: Vec<&str> = asset.version.split(".").collect();
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
      }
      Ok(parsed.unwrap())
    }
    else
    {
      Err(format!("biffed it!"))
    }
  }
}

// #region Deserializers
pub fn deserialize_from_i32_to_enum<'de, D, T>(deserializer: D) -> Result<T, D::Error>
where D: Deserializer<'de>, T: From<i32> + Undefinable
{
  let val = i32::deserialize(deserializer)?;
  match val {
    n if T::from(n).is_undefined() => {
      let type_name = type_name::<T>().split("::").last().unwrap_or("unknown");
      Err(Error::custom(format!("Invalid {} field!", type_name)))
    },
    v => Ok(T::from(v))
  }
}

pub fn deserialize_from_option_i32_to_enum<'de, D, T>(deserializer: D) -> Result<Option<T>, D::Error>
where D: Deserializer<'de>, T: From<i32> + Undefinable
{
  let val = Option::<i32>::deserialize(deserializer)?;
  match val {
    Some(n) if T::from(n).is_undefined() => {
      let type_name = type_name::<T>().split("::").last().unwrap_or("unknown");
      Err(Error::custom(format!("Invalid {} field!", type_name)))
    },
    v => Ok(v.map(T::from))
  }
}

pub fn deserialize_from_string_to_enum<'de, D, T>(deserializer: D) -> Result<T, D::Error>
where D: Deserializer<'de>, T: From<String> + Undefinable
{
  let val = String::deserialize(deserializer)?;
  match val {
    n if T::from(n.clone()).is_undefined() => {
      let type_name = type_name::<T>().split("::").last().unwrap_or("unknown");
      Err(Error::custom(format!("Invalid {} field!", type_name)))
    },
    v => Ok(T::from(v))
  }
}

pub fn deserialize_from_option_string_to_enum<'de, D, T>(deserializer: D) -> Result<Option<T>, D::Error>
where D: Deserializer<'de>, T: From<String> + Undefinable
{
  let val = Option::<String>::deserialize(deserializer)?;
  match val {
    Some(n) if T::from(n.clone()).is_undefined() => {
      let type_name = type_name::<T>().split("::").last().unwrap_or("unknown");
      Err(Error::custom(format!("Invalid {} field!", type_name)))
    },
    v => Ok(v.map(T::from))
  }
}
// #endregion

// #region Serializers
pub fn serialize_to_i32<S, T>(val: &T, serializer: S) -> Result<S::Ok, S::Error>
where S: Serializer, T: Into<i32> + Copy
{
  serializer.serialize_i32((*val).into())
}

pub fn serialize_option_to_i32<S, T>(val: &Option<T>, serializer: S) -> Result<S::Ok, S::Error>
where S: Serializer, T: Into<i32> + Copy
{
  match val {
    Some(v) => serializer.serialize_i32((*v).into()),
    _ => serializer.serialize_none()
  }
}

pub fn serialize_to_str<'se, S, T>(val: &T, serializer: S) -> Result<S::Ok, S::Error>
where S: Serializer, T: Into<&'se str> + Copy
{
  serializer.serialize_str((*val).into())
}

pub fn serialize_option_to_str<'se, S, T>(val: &Option<T>, serializer: S) -> Result<S::Ok, S::Error>
where S: Serializer, T: Into<&'se str> + Copy
{
  match val {
    Some(v) => serializer.serialize_str((*v).into()),
    _ => serializer.serialize_none()
  }
}
// #endregion

// #region Defaults
pub fn default_base_color_factor() -> [f32; 4] { [1.0, 1.0, 1.0, 1.0] }
pub fn default_emissive_factor() -> [f32; 3] { [0.0, 0.0, 0.0] }
pub fn default_f32_1() -> f32 { 1.0 }
pub fn default_f32_half() -> f32 { 0.5 }
pub fn default_matrix() -> [f32; 16] { [ 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0 ] }
pub fn default_mesh_primitive_mode() -> MeshPrimitiveMode { MeshPrimitiveMode::from(4) }
pub fn default_rotation() -> [f32; 4] { [ 0.0, 0.0, 0.0, 1.0] }
pub fn default_scale() -> [f32; 3] { [1.0, 1.0, 1.0] }
pub fn default_translation() -> [f32; 3] { [0.0, 0.0, 0.0] }
// #endregion






