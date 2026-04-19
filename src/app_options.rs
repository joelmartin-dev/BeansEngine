use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize, Debug, Default)]
pub struct Resolution(pub u32, pub u32);

#[derive(Serialize, Deserialize, Debug, Default)]
pub struct AppOptions {
  pub resolution: Resolution,
  pub frames_in_flight: usize
}