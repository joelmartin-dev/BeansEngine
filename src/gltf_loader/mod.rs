pub struct Extension {}
pub struct Accessor {}
pub struct Animation {}
pub struct Asset {}
pub struct Buffer {}
pub struct BufferView {}
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
  scene: Option<i32>,
  // An array of scenes.
  scenes: Option<Vec<Scene>>,
  // An array of skins.  A skin is defined by joints and matrices.
  skins: Option<Vec<Skin>>,
  // An array of textures.
  textures: Option<Vec<Texture>>
}