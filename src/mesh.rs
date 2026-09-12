use glm::{ Vec2, Vec3 };

/* vao, ibo and Vertex */

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Vertex {
    pub pos:   Vec2,
    pub color: Vec3,
    pub uv: Vec2,
}

impl Vertex {
    pub fn get_binding_description() -> ash::vk::VertexInputBindingDescription {
        ash::vk::VertexInputBindingDescription {
            binding: 0u32,
            stride: size_of::<Vertex>() as u32,
            input_rate: ash::vk::VertexInputRate::VERTEX,
        }
    }

    pub fn get_attribute_descriptions() -> [ash::vk::VertexInputAttributeDescription; 3] {
        let description_1 = ash::vk::VertexInputAttributeDescription::default()
            .location(0u32)
            .binding(0u32)
            .format(ash::vk::Format::R32G32_SFLOAT)
            .offset(std::mem::offset_of!(Vertex, pos) as u32);

        let description_2 = ash::vk::VertexInputAttributeDescription::default()
            .location(1u32)
            .binding(0u32)
            .format(ash::vk::Format::R32G32B32_SFLOAT)
            .offset(std::mem::offset_of!(Vertex, color) as u32);

        let description_3 = ash::vk::VertexInputAttributeDescription::default()
            .location(2_u32)
            .binding(0_u32)
            .format(ash::vk::Format::R32G32_SFLOAT)
            .offset(std::mem::offset_of!(Vertex, uv) as u32);

        [description_1, description_2, description_3]
    }
}

pub struct Mesh {
    pub vertices: Vec<Vertex>,
    pub indices:  Vec<u16>
}

impl Mesh {
    pub fn data() -> Self {
        Self {
            vertices: vec![
            Vertex { pos: Vec2::new(-0.5, -0.5), color: Vec3::new(1.0, 0.0, 0.0), uv: Vec2::new(1.0, 0.0)},
            Vertex { pos: Vec2::new( 0.5, -0.5), color: Vec3::new(1.0, 1.0, 0.0), uv: Vec2::new(0.0, 0.0)},
            Vertex { pos: Vec2::new( 0.5,  0.5), color: Vec3::new(0.0, 0.0, 1.0), uv: Vec2::new(0.0, 1.0)},
            Vertex { pos: Vec2::new(-0.5,  0.5), color: Vec3::new(1.0, 1.0, 1.0), uv: Vec2::new(1.0, 1.0)},
        ],
            indices: vec![
            0, 2, 1,
            2, 0, 3
        ]}
    }
}
