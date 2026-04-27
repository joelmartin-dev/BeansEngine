use std::{any::{type_name}, fs, path::PathBuf};

use pct_str::{PctString, UriReserved};
use serde::{Deserialize, Deserializer, Serializer, de::Error};

use crate::gltf_loader::{GltfLoader, Validatable, enums::{MeshPrimitiveMode, Undefinable}};

impl GltfLoader {
  pub fn load(path: &PathBuf) -> Result<Self, String> {
    let parsed: Result<GltfLoader, serde_json::Error> = serde_json::from_str(&fs::read_to_string(path).unwrap());

    if let Ok(loaded) = parsed {
      loaded.is_valid(&loaded)?;
      
      if let Some(accessors)    = &loaded.accessors     
        { accessors   .iter().try_for_each(|accessor|     accessor    .is_valid(&loaded))? };
      
      if let Some(animations)   = &loaded.animations    
        { animations  .iter().try_for_each(|animation|    animation   .is_valid(&loaded))? };
      
      loaded.asset.is_valid(&loaded)?;
      
      if let Some(buffers)      = &loaded.buffers       
        { buffers     .iter().try_for_each(|buffer|       buffer      .is_valid(&loaded))? };
      
      if let Some(buffer_views) = &loaded.buffer_views  
        { buffer_views.iter().try_for_each(|buffer_view|  buffer_view .is_valid(&loaded))? };
      
      if let Some(cameras)      = &loaded.cameras       
        { cameras     .iter().try_for_each(|camera|       camera      .is_valid(&loaded))? };
      
      if let Some(images)       = &loaded.images        
        { images      .iter().try_for_each(|image|        image       .is_valid(&loaded))? };
      
      if let Some(materials)    = &loaded.materials     
        { materials   .iter().try_for_each(|material|     material    .is_valid(&loaded))? };
      
      if let Some(meshes)       = &loaded.meshes        
        { meshes      .iter().try_for_each(|mesh|         mesh        .is_valid(&loaded))? };
      
      if let Some(nodes)        = &loaded.nodes         
        { nodes       .iter().try_for_each(|node|         node        .is_valid(&loaded))? };
      
      if let Some(samplers)     = &loaded.samplers      
        { samplers    .iter().try_for_each(|sampler|      sampler     .is_valid(&loaded))? };
      
      if let Some(scenes)       = &loaded.scenes        
        { scenes      .iter().try_for_each(|scene|        scene       .is_valid(&loaded))? };
      
      if let Some(skins)        = &loaded.skins         
        { skins       .iter().try_for_each(|skin|         skin        .is_valid(&loaded))? };
      
      if let Some(textures)     = &loaded.textures      
        { textures    .iter().try_for_each(|texture|      texture     .is_valid(&loaded))? };
      
      Ok(loaded)
    }
    else {
      Err(parsed.unwrap_err().to_string())
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

pub fn deserialize_string_to_iri<'de, D>(deserializer: D) -> Result<Option<String>, D::Error>
where D: Deserializer<'de>
{
  let val = Option::<String>::deserialize(deserializer)?;
  match val {
    Some(v) => { let encoded_val = PctString::encode(v.chars(), UriReserved::Path); Ok(Some(encoded_val.to_string()))},
    None => Ok(None)
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






