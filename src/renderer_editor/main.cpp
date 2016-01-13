#include "lumix.h"
#include "core/crc32.h"
#include "core/FS/file_system.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/resource_manager.h"
#include "editor/ieditor_command.h"
#include "editor/property_register.h"
#include "editor/property_descriptor.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "studio_lib/asset_browser.h"
#include "studio_lib/platform_interface.h"
#include "studio_lib/property_grid.h"
#include "studio_lib/studio_app.h"
#include "studio_lib/utils.h"
#include "terrain_editor.h"
#include <cfloat>


using namespace Lumix;


static const uint32 TEXTURE_HASH = ResourceManager::TEXTURE;
static const uint32 SHADER_HASH = ResourceManager::SHADER;


template <class S> class EntityEnumPropertyDescriptor : public IEnumPropertyDescriptor
{
public:
	typedef Entity(S::*Getter)(ComponentIndex);
	typedef void (S::*Setter)(ComponentIndex, Entity);
	typedef Entity(S::*ArrayGetter)(ComponentIndex, int);
	typedef void (S::*ArraySetter)(ComponentIndex, int, Entity);

public:
	EntityEnumPropertyDescriptor(const char* name,
		Getter _getter,
		Setter _setter,
		WorldEditor& editor,
		IAllocator& allocator)
		: IEnumPropertyDescriptor(allocator)
		, m_editor(editor)
	{
		setName(name);
		m_single.getter = _getter;
		m_single.setter = _setter;
		m_type = ENUM;
	}


	EntityEnumPropertyDescriptor(const char* name,
		ArrayGetter _getter,
		ArraySetter _setter,
		WorldEditor& editor,
		IAllocator& allocator)
		: IEnumPropertyDescriptor(allocator)
		, m_editor(editor)
	{
		setName(name);
		m_array.getter = _getter;
		m_array.setter = _setter;
		m_type = ENUM;
	}


	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		int value;
		stream.read(&value, sizeof(value));
		auto entity = value < 0 ? INVALID_ENTITY : m_editor.getUniverse()->getEntityFromDenseIdx(value);
		if (index == -1)
		{
			(static_cast<S*>(cmp.scene)->*m_single.setter)(cmp.index, entity);
		}
		else
		{
			(static_cast<S*>(cmp.scene)->*m_array.setter)(cmp.index, index, entity);
		}
	};


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		Entity value;
		if (index == -1)
		{
			value = (static_cast<S*>(cmp.scene)->*m_single.getter)(cmp.index);
		}
		else
		{
			value = (static_cast<S*>(cmp.scene)->*m_array.getter)(cmp.index, index);
		}
		auto dense_idx = m_editor.getUniverse()->getDenseIdx(value);
		int len = sizeof(dense_idx);
		stream.write(&dense_idx, len);
	};


	int getEnumCount(IScene* scene) override { return scene->getUniverse().getEntityCount(); }


	const char* getEnumItemName(IScene* scene, int index) override { return nullptr; }


	void getEnumItemName(IScene* scene, int index, char* buf, int max_size) override
	{
		auto entity = scene->getUniverse().getEntityFromDenseIdx(index);
		getEntityListDisplayName(m_editor, buf, max_size, entity);
	}

private:
	union
	{
		struct
		{
			Getter getter;
			Setter setter;
		} m_single;
		struct
		{
			ArrayGetter getter;
			ArraySetter setter;
		} m_array;
	};
	WorldEditor& m_editor;
};


void registerRendererProperties(WorldEditor& editor)
{
	IAllocator& allocator = editor.getAllocator();

	PropertyRegister::registerComponentType("camera", "Camera");
	PropertyRegister::registerComponentType("global_light", "Global light");
	PropertyRegister::registerComponentType("renderable", "Mesh");
	PropertyRegister::registerComponentType("particle_emitter", "Particle emitter");
	PropertyRegister::registerComponentType("particle_emitter_spawn_shape", "Particle emitter - spawn shape");
	PropertyRegister::registerComponentType("particle_emitter_fade", "Particle emitter - fade");
	PropertyRegister::registerComponentType("particle_emitter_plane", "Particle emitter - plane");
	PropertyRegister::registerComponentType("particle_emitter_force", "Particle emitter - force");
	PropertyRegister::registerComponentType("particle_emitter_attractor", "Particle emitter - attractor");
	PropertyRegister::registerComponentType(
		"particle_emitter_linear_movement", "Particle emitter - linear movement");
	PropertyRegister::registerComponentType(
		"particle_emitter_random_rotation", "Particle emitter - random rotation");
	PropertyRegister::registerComponentType("particle_emitter_size", "Particle emitter - size");
	PropertyRegister::registerComponentType("point_light", "Point light");
	PropertyRegister::registerComponentType("terrain", "Terrain");

	PropertyRegister::registerComponentDependency("particle_emitter_fade", "particle_emitter");
	PropertyRegister::registerComponentDependency("particle_emitter_force", "particle_emitter");
	PropertyRegister::registerComponentDependency(
		"particle_emitter_linear_movement", "particle_emitter");
	PropertyRegister::registerComponentDependency(
		"particle_emitter_random_rotation", "particle_emitter");

	PropertyRegister::add("particle_emitter_spawn_shape",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Radius",
		&RenderScene::getParticleEmitterShapeRadius,
		&RenderScene::setParticleEmitterShapeRadius,
		0.0f,
		FLT_MAX,
		0.01f,
		allocator));

	PropertyRegister::add("particle_emitter_plane",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Bounce",
		&RenderScene::getParticleEmitterPlaneBounce,
		&RenderScene::setParticleEmitterPlaneBounce,
		0.0f,
		1.0f,
		0.01f,
		allocator));
	auto plane_module_planes = LUMIX_NEW(allocator, ArrayDescriptor<RenderScene>)("Planes",
		&RenderScene::getParticleEmitterPlaneCount,
		&RenderScene::addParticleEmitterPlane,
		&RenderScene::removeParticleEmitterPlane,
		allocator);
	plane_module_planes->addChild(
		LUMIX_NEW(allocator, EntityEnumPropertyDescriptor<RenderScene>)("Entity",
		&RenderScene::getParticleEmitterPlaneEntity,
		&RenderScene::setParticleEmitterPlaneEntity,
		editor,
		allocator));
	PropertyRegister::add("particle_emitter_plane", plane_module_planes);

	PropertyRegister::add("particle_emitter_attractor",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Force",
		&RenderScene::getParticleEmitterAttractorForce,
		&RenderScene::setParticleEmitterAttractorForce,
		-FLT_MAX,
		FLT_MAX,
		0.01f,
		allocator));
	auto attractor_module_planes = LUMIX_NEW(allocator, ArrayDescriptor<RenderScene>)("Attractors",
		&RenderScene::getParticleEmitterAttractorCount,
		&RenderScene::addParticleEmitterAttractor,
		&RenderScene::removeParticleEmitterAttractor,
		allocator);
	attractor_module_planes->addChild(
		LUMIX_NEW(allocator, EntityEnumPropertyDescriptor<RenderScene>)("Entity",
		&RenderScene::getParticleEmitterAttractorEntity,
		&RenderScene::setParticleEmitterAttractorEntity,
		editor,
		allocator));
	PropertyRegister::add("particle_emitter_attractor", attractor_module_planes);

	PropertyRegister::add("particle_emitter_fade",
		LUMIX_NEW(allocator, SampledFunctionDescriptor<RenderScene>)("Alpha",
		&RenderScene::getParticleEmitterAlpha,
		&RenderScene::setParticleEmitterAlpha,
		&RenderScene::getParticleEmitterAlphaCount,
		1,
		1,
		allocator));

	PropertyRegister::add("particle_emitter_force",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, RenderScene>)("Acceleration",
		&RenderScene::getParticleEmitterAcceleration,
		&RenderScene::setParticleEmitterAcceleration,
		allocator));

	PropertyRegister::add("particle_emitter_size",
		LUMIX_NEW(allocator, SampledFunctionDescriptor<RenderScene>)("Size",
		&RenderScene::getParticleEmitterSize,
		&RenderScene::setParticleEmitterSize,
		&RenderScene::getParticleEmitterSizeCount,
		1,
		1,
		allocator));

	PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("x",
		&RenderScene::getParticleEmitterLinearMovementX,
		&RenderScene::setParticleEmitterLinearMovementX,
		allocator));
	PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("y",
		&RenderScene::getParticleEmitterLinearMovementY,
		&RenderScene::setParticleEmitterLinearMovementY,
		allocator));
	PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("z",
		&RenderScene::getParticleEmitterLinearMovementZ,
		&RenderScene::setParticleEmitterLinearMovementZ,
		allocator));

	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("Life",
		&RenderScene::getParticleEmitterInitialLife,
		&RenderScene::setParticleEmitterInitialLife,
		allocator));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("Initial size",
		&RenderScene::getParticleEmitterInitialSize,
		&RenderScene::setParticleEmitterInitialSize,
		allocator));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("Spawn period",
		&RenderScene::getParticleEmitterSpawnPeriod,
		&RenderScene::setParticleEmitterSpawnPeriod,
		allocator));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Int2, RenderScene>)("Spawn count",
		&RenderScene::getParticleEmitterSpawnCount,
		&RenderScene::setParticleEmitterSpawnCount,
		allocator));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Material",
		&RenderScene::getParticleEmitterMaterialPath,
		&RenderScene::setParticleEmitterMaterialPath,
		"Material (*.mat)",
		ResourceManager::MATERIAL,
		allocator));

	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, StringPropertyDescriptor<RenderScene>)("Slot",
		&RenderScene::getCameraSlot,
		&RenderScene::setCameraSlot,
		allocator));
	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("FOV",
		&RenderScene::getCameraFOV,
		&RenderScene::setCameraFOV,
		1.0f,
		179.0f,
		1.0f,
		allocator));
	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Near",
		&RenderScene::getCameraNearPlane,
		&RenderScene::setCameraNearPlane,
		0.0f,
		FLT_MAX,
		0.0f,
		allocator));
	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Far",
		&RenderScene::getCameraFarPlane,
		&RenderScene::setCameraFarPlane,
		0.0f,
		FLT_MAX,
		0.0f,
		allocator));

	PropertyRegister::add("renderable",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Source",
		&RenderScene::getRenderablePath,
		&RenderScene::setRenderablePath,
		"Mesh (*.msh)",
		ResourceManager::MODEL,
		allocator));

	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Ambient intensity",
		&RenderScene::getLightAmbientIntensity,
		&RenderScene::setLightAmbientIntensity,
		0.0f,
		1.0f,
		0.05f,
		allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec4, RenderScene>)("Shadow cascades",
		&RenderScene::getShadowmapCascades,
		&RenderScene::setShadowmapCascades,
		allocator));

	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Diffuse intensity",
		&RenderScene::getGlobalLightIntensity,
		&RenderScene::setGlobalLightIntensity,
		0.0f,
		1.0f,
		0.05f,
		allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Fog density",
		&RenderScene::getFogDensity,
		&RenderScene::setFogDensity,
		0.0f,
		1.0f,
		0.01f,
		allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Fog bottom",
		&RenderScene::getFogBottom,
		&RenderScene::setFogBottom,
		-FLT_MAX,
		FLT_MAX,
		1.0f,
		allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Fog height",
		&RenderScene::getFogHeight,
		&RenderScene::setFogHeight,
		0.01f,
		FLT_MAX,
		1.0f,
		allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Ambient color",
		&RenderScene::getLightAmbientColor,
		&RenderScene::setLightAmbientColor,
		allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Diffuse color",
		&RenderScene::getGlobalLightColor,
		&RenderScene::setGlobalLightColor,
		allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Fog color",
		&RenderScene::getFogColor,
		&RenderScene::setFogColor,
		allocator));

	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<RenderScene>)("Cast shadows",
		&RenderScene::getLightCastShadows,
		&RenderScene::setLightCastShadows,
		allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Diffuse intensity",
		&RenderScene::getPointLightIntensity,
		&RenderScene::setPointLightIntensity,
		0.0f,
		1.0f,
		0.05f,
		allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Diffuse color",
		&RenderScene::getPointLightColor,
		&RenderScene::setPointLightColor,
		allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Specular color",
		&RenderScene::getPointLightSpecularColor,
		&RenderScene::setPointLightSpecularColor,
		allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("FOV",
		&RenderScene::getLightFOV,
		&RenderScene::setLightFOV,
		0.0f,
		360.0f,
		5.0f,
		allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Attenuation",
		&RenderScene::getLightAttenuation,
		&RenderScene::setLightAttenuation,
		0.0f,
		1000.0f,
		0.1f,
		allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Range",
		&RenderScene::getLightRange,
		&RenderScene::setLightRange,
		0.0f,
		FLT_MAX,
		1.0f,
		allocator));
	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Material",
		&RenderScene::getTerrainMaterialPath,
		&RenderScene::setTerrainMaterialPath,
		"Material (*.mat)",
		ResourceManager::MATERIAL,
		allocator));
	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("XZ scale",
		&RenderScene::getTerrainXZScale,
		&RenderScene::setTerrainXZScale,
		0.0f,
		FLT_MAX,
		0.0f,
		allocator));
	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Height scale",
		&RenderScene::getTerrainYScale,
		&RenderScene::setTerrainYScale,
		0.0f,
		FLT_MAX,
		0.0f,
		allocator));

	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, IntPropertyDescriptor<RenderScene>)("Grass distance",
		&RenderScene::getGrassDistance,
		&RenderScene::setGrassDistance,
		allocator));

	auto grass = LUMIX_NEW(allocator, ArrayDescriptor<RenderScene>)("Grass",
		&RenderScene::getGrassCount,
		&RenderScene::addGrass,
		&RenderScene::removeGrass,
		allocator);
	grass->addChild(LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Mesh",
		&RenderScene::getGrassPath,
		&RenderScene::setGrassPath,
		"Mesh (*.msh)",
		crc32("model"),
		allocator));
	auto ground = LUMIX_NEW(allocator, IntPropertyDescriptor<RenderScene>)("Ground",
		&RenderScene::getGrassGround,
		&RenderScene::setGrassGround,
		allocator);
	ground->setLimit(0, 4);
	grass->addChild(ground);
	grass->addChild(LUMIX_NEW(allocator, IntPropertyDescriptor<RenderScene>)("Density",
		&RenderScene::getGrassDensity,
		&RenderScene::setGrassDensity,
		allocator));
	PropertyRegister::add("terrain", grass);
}


static const uint32 MATERIAL_HASH = crc32("MATERIAL");


struct MaterialPlugin : public AssetBrowser::IPlugin
{
	MaterialPlugin(StudioApp& app)
		: m_app(app)
	{
	}

		
	void saveMaterial(Material* material)
	{
		FS::FileSystem& fs = m_app.getWorldEditor()->getEngine().getFileSystem();
		// use temporary because otherwise the material is reloaded during saving
		char tmp_path[MAX_PATH_LENGTH];
		strcpy(tmp_path, material->getPath().c_str());
		strcat(tmp_path, ".tmp");
		FS::IFile* file = fs.open(fs.getDefaultDevice(),
			Path(tmp_path),
			FS::Mode::CREATE | FS::Mode::WRITE);
		if (file)
		{
			DefaultAllocator allocator;
			JsonSerializer serializer(
				*file, JsonSerializer::AccessMode::WRITE, material->getPath(), allocator);
			if (!material->save(serializer))
			{
				g_log_error.log("Material manager") << "Error saving "
					<< material->getPath().c_str();
			}
			fs.close(*file);

			PlatformInterface::deleteFile(material->getPath().c_str());
			PlatformInterface::moveFile(tmp_path, material->getPath().c_str());
		}
		else
		{
			g_log_error.log("Material manager") << "Could not save file "
				<< material->getPath().c_str();
		}
	}


	bool onGUI(Resource* resource, uint32 type) override
	{
		if (type != MATERIAL_HASH) return false;

		auto* material = static_cast<Material*>(resource);

		if (ImGui::Button("Save")) saveMaterial(material);
		ImGui::SameLine();
		if (ImGui::Button("Open in external editor")) m_app.getAssetBrowser()->openInExternalEditor(material);

		bool b;
		if (material->hasAlphaCutoutDefine())
		{
			b = material->isAlphaCutout();
			if (ImGui::Checkbox("Is alpha cutout", &b)) material->enableAlphaCutout(b);
		}

		b = material->isBackfaceCulling();
		if (ImGui::Checkbox("Is backface culling", &b)) material->enableBackfaceCulling(b);

		if (material->hasShadowReceivingDefine())
		{
			b = material->isShadowReceiver();
			if (ImGui::Checkbox("Is shadow receiver", &b)) material->enableShadowReceiving(b);
		}

		b = material->isZTest();
		if (ImGui::Checkbox("Z test", &b)) material->enableZTest(b);

		Vec3 specular = material->getSpecular();
		if (ImGui::ColorEdit3("Specular", &specular.x))
		{
			material->setSpecular(specular);
		}

		float shininess = material->getShininess();
		if (ImGui::DragFloat("Shininess", &shininess))
		{
			material->setShininess(shininess);
		}

		char buf[256];
		copyString(buf, material->getShader() ? material->getShader()->getPath().c_str() : "");
		if (m_app.getAssetBrowser()->resourceInput("Shader", "shader", buf, sizeof(buf), SHADER_HASH))
		{
			material->setShader(Path(buf));
		}

		for (int i = 0; i < material->getShader()->getTextureSlotCount(); ++i)
		{
			auto& slot = material->getShader()->getTextureSlot(i);
			auto* texture = material->getTexture(i);
			copyString(buf, texture ? texture->getPath().c_str() : "");
			if (m_app.getAssetBrowser()->resourceInput(
				slot.m_name, StringBuilder<30>("", (uint64)&slot), buf, sizeof(buf), TEXTURE_HASH))
			{
				material->setTexturePath(i, Path(buf));
			}
			if (!texture) continue;

			ImGui::SameLine();
			StringBuilder<100> popup_name("pu", (uint64)texture, slot.m_name);
			if (ImGui::Button(StringBuilder<100>("Advanced###adv", (uint64)texture, slot.m_name)))
			{
				ImGui::OpenPopup(popup_name);
			}

			if (ImGui::BeginPopup(popup_name))
			{
				bool u_clamp = (texture->getFlags() & BGFX_TEXTURE_U_CLAMP) != 0;
				if (ImGui::Checkbox("u clamp", &u_clamp))
				{
					texture->setFlag(BGFX_TEXTURE_U_CLAMP, u_clamp);
				}
				bool v_clamp = (texture->getFlags() & BGFX_TEXTURE_V_CLAMP) != 0;
				if (ImGui::Checkbox("v clamp", &v_clamp))
				{
					texture->setFlag(BGFX_TEXTURE_V_CLAMP, v_clamp);
				}
				bool min_point = (texture->getFlags() & BGFX_TEXTURE_MIN_POINT) != 0;
				if (ImGui::Checkbox("Min point", &min_point))
				{
					texture->setFlag(BGFX_TEXTURE_MIN_POINT, min_point);
				}
				bool mag_point = (texture->getFlags() & BGFX_TEXTURE_MAG_POINT) != 0;
				if (ImGui::Checkbox("Mag point", &mag_point))
				{
					texture->setFlag(BGFX_TEXTURE_MAG_POINT, mag_point);
				}
				if (slot.m_is_atlas)
				{
					int size = texture->getAtlasSize() - 2;
					const char values[] = { '2', 'x', '2', 0, '3', 'x', '3', 0, '4', 'x', '4', 0, 0 };
					if (ImGui::Combo(StringBuilder<30>("Atlas size###", i), &size, values))
					{
						texture->setAtlasSize(size + 2);
					}
				}
				ImGui::EndPopup();

			}

		}

		for (int i = 0; i < material->getUniformCount(); ++i)
		{
			auto& uniform = material->getUniform(i);
			switch (uniform.m_type)
			{
			case Material::Uniform::FLOAT:
				ImGui::DragFloat(uniform.m_name, &uniform.m_float);
				break;
			}
		}
		ImGui::Columns(1);
		return true;
	}


	void onResourceUnloaded(Resource* resource) override
	{
	}


	const char* getName() const override
	{
		return "Material";
	}


	bool hasResourceManager(uint32 type) const override
	{
		return type == MATERIAL_HASH;
	}


	uint32 getResourceType(const char* ext) override
	{
		if (compareString(ext, "mat") == 0) return MATERIAL_HASH;
		return 0;
	}


	StudioApp& m_app;
};


static const uint32 MODEL_HASH = ResourceManager::MODEL;


struct ModelPlugin : public AssetBrowser::IPlugin
{
	struct InsertMeshCommand : public IEditorCommand
	{
		Vec3 m_position;
		Path m_mesh_path;
		Entity m_entity;
		WorldEditor& m_editor;

		Entity getEntity() const { return m_entity; }
		InsertMeshCommand(WorldEditor& editor)
			: m_editor(editor)
		{
		}


		InsertMeshCommand(WorldEditor& editor,
			const Vec3& position,
			const Path& mesh_path)
			: m_mesh_path(mesh_path)
			, m_position(position)
			, m_editor(editor)
		{
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("path", m_mesh_path.c_str());
			serializer.beginArray("pos");
			serializer.serializeArrayItem(m_position.x);
			serializer.serializeArrayItem(m_position.y);
			serializer.serializeArrayItem(m_position.z);
			serializer.endArray();
		}


		void deserialize(JsonSerializer& serializer) override
		{
			char path[MAX_PATH_LENGTH];
			serializer.deserialize("path", path, sizeof(path), "");
			m_mesh_path = path;
			serializer.deserializeArrayBegin("pos");
			serializer.deserializeArrayItem(m_position.x, 0);
			serializer.deserializeArrayItem(m_position.y, 0);
			serializer.deserializeArrayItem(m_position.z, 0);
			serializer.deserializeArrayEnd();
		}


		bool execute() override
		{
			static const uint32 RENDERABLE_HASH = crc32("renderable");

			Universe* universe = m_editor.getUniverse();
			m_entity = universe->createEntity(Vec3(0, 0, 0), Quat(0, 0, 0, 1));
			universe->setPosition(m_entity, m_position);
			const Array<IScene*>& scenes = m_editor.getScenes();
			ComponentIndex cmp = -1;
			IScene* scene = nullptr;
			for (int i = 0; i < scenes.size(); ++i)
			{
				cmp = scenes[i]->createComponent(RENDERABLE_HASH, m_entity);

				if (cmp >= 0)
				{
					scene = scenes[i];
					break;
				}
			}
			if (cmp >= 0)
			{
				char rel_path[MAX_PATH_LENGTH];
				m_editor.getRelativePath(rel_path, MAX_PATH_LENGTH, m_mesh_path.c_str());
				static_cast<RenderScene*>(scene)->setRenderablePath(cmp, Path(rel_path));
			}
			return true;
		}


		void undo() override
		{
			const WorldEditor::ComponentList& cmps =
				m_editor.getComponents(m_entity);
			for (int i = 0; i < cmps.size(); ++i)
			{
				cmps[i].scene->destroyComponent(cmps[i].index, cmps[i].type);
			}
			m_editor.getUniverse()->destroyEntity(m_entity);
			m_entity = INVALID_ENTITY;
		}


		uint32 getType() override
		{
			static const uint32 type = crc32("insert_mesh");
			return type;
		}


		bool merge(IEditorCommand&) override
		{
			return false;
		}


	};


	ModelPlugin(StudioApp& app)
		: m_app(app)
	{
		m_app.getWorldEditor()->registerEditorCommandCreator("insert_mesh", createInsertMeshCommand);

	}


	static IEditorCommand* createInsertMeshCommand(WorldEditor& editor)
	{
		return LUMIX_NEW(editor.getAllocator(), InsertMeshCommand)(editor);
	}


	static void insertInScene(WorldEditor& editor, Model* model)
	{
		auto* command = LUMIX_NEW(editor.getAllocator(), InsertMeshCommand)(
			editor, editor.getCameraRaycastHit(), model->getPath());

		editor.executeCommand(command);
	}


	bool onGUI(Resource* resource, uint32 type) override
	{
		if (type != MODEL_HASH) return false;

		auto* model = static_cast<Model*>(resource);
		if (ImGui::Button("Insert in scene"))
		{
			insertInScene(*m_app.getWorldEditor(), model);
		}

		ImGui::LabelText("Bone count", "%d", model->getBoneCount());
		if (model->getBoneCount() > 0 && ImGui::CollapsingHeader("Bones"))
		{
			for (int i = 0; i < model->getBoneCount(); ++i)
			{
				ImGui::Text(model->getBone(i).name.c_str());
			}
		}

		ImGui::LabelText("Bounding radius", "%f", model->getBoundingRadius());

		auto& lods = model->getLODs();
		if (!lods.empty())
		{
			ImGui::Separator();
			ImGui::Columns(3);
			ImGui::Text("LOD"); ImGui::NextColumn();
			ImGui::Text("Distance"); ImGui::NextColumn();
			ImGui::Text("# of meshes"); ImGui::NextColumn();
			ImGui::Separator();
			for (int i = 0; i < lods.size() - 1; ++i)
			{
				ImGui::Text("%d", i); ImGui::NextColumn();
				ImGui::DragFloat("", &lods[i].m_distance); ImGui::NextColumn();
				ImGui::Text("%d", lods[i].m_to_mesh - lods[i].m_from_mesh + 1); ImGui::NextColumn();
			}

			ImGui::Text("%d", lods.size() - 1); ImGui::NextColumn();
			ImGui::Text("INFINITE"); ImGui::NextColumn();
			ImGui::Text("%d", lods.back().m_to_mesh - lods.back().m_from_mesh + 1);
			ImGui::Columns(1);
		}

		ImGui::Separator();
		for (int i = 0; i < model->getMeshCount(); ++i)
		{
			auto& mesh = model->getMesh(i);
			if (ImGui::TreeNode(&mesh, mesh.getName()[0] != 0 ? mesh.getName() : "N/A"))
			{
				ImGui::LabelText("Triangle count", "%d", mesh.getTriangleCount());
				ImGui::LabelText("Material", mesh.getMaterial()->getPath().c_str());
				ImGui::SameLine();
				if (ImGui::Button("->"))
				{
					m_app.getAssetBrowser()->selectResource(mesh.getMaterial()->getPath());
				}
				ImGui::TreePop();
			}
		}
		return true;
	}


	void onResourceUnloaded(Resource* resource) override
	{
	}


	const char* getName() const override
	{
		return "Model";
	}


	bool hasResourceManager(uint32 type) const override
	{
		return type == MODEL_HASH;
	}


	uint32 getResourceType(const char* ext) override
	{
		if (compareString(ext, "msh") == 0) return MODEL_HASH;
		return 0;
	}


	StudioApp& m_app;
};


struct TexturePlugin : public AssetBrowser::IPlugin
{
	TexturePlugin(StudioApp& app)
		: m_app(app)
	{
	}

	
	bool onGUI(Resource* resource, uint32 type) override
	{
		if (type != TEXTURE_HASH) return false;

		auto* texture = static_cast<Texture*>(resource);
		if (texture->isFailure())
		{
			ImGui::Text("Texture failed to load");
			return true;
		}

		ImGui::LabelText("Size", "%dx%d", texture->getWidth(), texture->getHeight());
		ImGui::LabelText("BPP", "%d", texture->getBytesPerPixel());
		m_texture_handle = texture->getTextureHandle();
		if (bgfx::isValid(m_texture_handle))
		{
			ImGui::Image(&m_texture_handle, ImVec2(200, 200));
			if (ImGui::Button("Open"))
			{
				m_app.getAssetBrowser()->openInExternalEditor(resource);
			}
		}
		return true;
	}


	void onResourceUnloaded(Resource* resource) override
	{
	}


	const char* getName() const override
	{
		return "Texture";
	}


	bool hasResourceManager(uint32 type) const override
	{
		return type == TEXTURE_HASH;
	}


	uint32 getResourceType(const char* ext) override
	{
		if (compareString(ext, "tga") == 0) return TEXTURE_HASH;
		if (compareString(ext, "dds") == 0) return TEXTURE_HASH;
		if (compareString(ext, "raw") == 0) return TEXTURE_HASH;
		return 0;
	}

	bgfx::TextureHandle m_texture_handle;
	StudioApp& m_app;
};


struct ShaderPlugin : public AssetBrowser::IPlugin
{
	ShaderPlugin(StudioApp& app)
		: m_app(app)
	{
	}


	bool onGUI(Resource* resource, uint32 type) override
	{
		if (type != SHADER_HASH) return false;
		auto* shader = static_cast<Shader*>(resource);
		StringBuilder<MAX_PATH_LENGTH> path(m_app.getWorldEditor()->getBasePath());
		char basename[MAX_PATH_LENGTH];
		PathUtils::getBasename(
			basename, lengthOf(basename), resource->getPath().c_str());
		path << "/shaders/" << basename;
		if (ImGui::Button("Open vertex shader"))
		{
			path << "_vs.sc";
			PlatformInterface::shellExecuteOpen(path);
		}
		ImGui::SameLine();
		if (ImGui::Button("Open fragment shader"))
		{
			path << "_fs.sc";
			PlatformInterface::shellExecuteOpen(path);
		}

		if (ImGui::CollapsingHeader("Texture slots", nullptr, true, true))
		{
			ImGui::Columns(2);
			ImGui::Text("name");
			ImGui::NextColumn();
			ImGui::Text("uniform");
			ImGui::NextColumn();
			ImGui::Separator();
			for (int i = 0; i < shader->getTextureSlotCount(); ++i)
			{
				auto& slot = shader->getTextureSlot(i);
				ImGui::Text(slot.m_name);
				ImGui::NextColumn();
				ImGui::Text(slot.m_uniform);
				ImGui::NextColumn();
			}
			ImGui::Columns(1);
		}
		return true;
	}


	void onResourceUnloaded(Resource* resource) override
	{
	}


	const char* getName() const override
	{
		return "Shader";
	}


	bool hasResourceManager(uint32 type) const override
	{
		return type == SHADER_HASH;
	}


	uint32 getResourceType(const char* ext) override
	{
		if (compareString(ext, "shd") == 0) return SHADER_HASH;
		return 0;
	}


	StudioApp& m_app;
};


static const uint32 PARTICLE_EMITTER_HASH = crc32("particle_emitter");


struct EmitterPlugin : public PropertyGrid::IPlugin
{
	EmitterPlugin(StudioApp& app)
		: m_app(app)
	{
		m_particle_emitter_updating = true;
		m_particle_emitter_timescale = 1.0f;
	}


	void onGUI(PropertyGrid& grid, ComponentUID cmp) override
	{
		if (cmp.type != PARTICLE_EMITTER_HASH) return;
		
		ImGui::Separator();
		ImGui::Checkbox("Update", &m_particle_emitter_updating);
		auto* scene = static_cast<Lumix::RenderScene*>(cmp.scene);
		ImGui::SameLine();
		if (ImGui::Button("Reset")) scene->resetParticleEmitter(cmp.index);

		if (m_particle_emitter_updating)
		{
			ImGui::DragFloat("Timescale", &m_particle_emitter_timescale, 0.01f, 0.01f, 10000.0f);
			float time_delta = m_app.getWorldEditor()->getEngine().getLastTimeDelta();
			scene->updateEmitter(cmp.index, time_delta * m_particle_emitter_timescale);
			scene->drawEmitterGizmo(cmp.index);
		}
	}


	StudioApp& m_app;
	float m_particle_emitter_timescale;
	bool m_particle_emitter_updating;
};


static const uint32 TERRAIN_HASH = crc32("terrain");


struct TerrainPlugin : public PropertyGrid::IPlugin
{
	TerrainPlugin(StudioApp& app)
		: m_app(app)
	{
		auto& editor = *app.getWorldEditor();
		m_terrain_editor = LUMIX_NEW(editor.getAllocator(), TerrainEditor)(editor, app.getActions());
	}


	~TerrainPlugin()
	{
		LUMIX_DELETE(m_app.getWorldEditor()->getAllocator(), m_terrain_editor);
	}


	void onGUI(PropertyGrid& grid, ComponentUID cmp) override
	{
		if (cmp.type != TERRAIN_HASH) return;

		m_terrain_editor->setComponent(cmp);
		m_terrain_editor->onGUI();
	}


	StudioApp& m_app;
	TerrainEditor* m_terrain_editor;
};


extern "C"
{
	LUMIX_LIBRARY_EXPORT void setStudioApp(StudioApp& app)
	{
		auto& allocator = app.getWorldEditor()->getAllocator();
		registerRendererProperties(*app.getWorldEditor());

		auto* material_plugin =
			LUMIX_NEW(app.getWorldEditor()->getAllocator(), MaterialPlugin)(app);
		app.getAssetBrowser()->addPlugin(*material_plugin);
		
		auto* model_plugin = LUMIX_NEW(app.getWorldEditor()->getAllocator(), ModelPlugin)(app);
		app.getAssetBrowser()->addPlugin(*model_plugin);

		auto* texture_plugin = LUMIX_NEW(app.getWorldEditor()->getAllocator(), TexturePlugin)(app);
		app.getAssetBrowser()->addPlugin(*texture_plugin);
		
		auto* shader_plugin = LUMIX_NEW(app.getWorldEditor()->getAllocator(), ShaderPlugin)(app);
		app.getAssetBrowser()->addPlugin(*shader_plugin);

		auto* emitter_plugin = LUMIX_NEW(app.getWorldEditor()->getAllocator(), EmitterPlugin)(app);
		app.getPropertyGrid()->addPlugin(*emitter_plugin);

		auto* terrain_plugin = LUMIX_NEW(app.getWorldEditor()->getAllocator(), TerrainPlugin)(app);
		app.getPropertyGrid()->addPlugin(*terrain_plugin);

	}
}