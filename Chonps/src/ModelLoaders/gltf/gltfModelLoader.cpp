#include "cepch.h"
#include "gltfModelLoader.h"

#include "Core/File.h"

#include <glm/gtc/type_ptr.hpp>

namespace Chonps
{
	std::vector<Mesh> loadgltfModel(const std::string& filepath)
	{
		std::vector<Mesh> Meshes;

		gltfModel gltf(filepath, false);
		Meshes = gltf.GetMeshes();

		return Meshes;
	}

	Mesh loadgltfModelBatched(const std::string& filepath)
	{
		gltfModel gltf(filepath, true);

		std::vector<vertex> vertices;
		std::vector<vertex> indices;

		std::vector<Mesh> meshes = gltf.GetMeshes();
		for (auto& mesh : meshes)
		{

		}

		return gltf.GetMeshes().front();
	}

	gltfModel::gltfModel(const std::string& filepath, bool batched)
		: m_Batched(batched)
	{
		std::string text = get_file_contents(filepath.c_str());
		JSON = json::parse(text);

		m_File = filepath.c_str();
		m_Data = getData();

		for (unsigned int i = 0; i < JSON["scenes"][0]["nodes"].size(); i++)
			traverseNode(JSON["scenes"][0]["nodes"][i]);
	}

	void gltfModel::traverseNode(unsigned int nextNode, glm::mat4 matrix)
	{
		json node = JSON["nodes"][nextNode];

		glm::vec3 nodeTranslation = glm::vec3(0.0f, 0.0f, 0.0f);
		if (node.find("translation") != node.end())
		{
			float transValues[3];
			for (unsigned int i = 0; i < node["translation"].size(); i++)
				transValues[i] = (node["translation"][i]);
			nodeTranslation = glm::make_vec3(transValues);
		}

		glm::quat nodeRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // glm::quat has order (w, x, y, z)
		if (node.find("rotation") != node.end())
		{
			float rotValues[4] =
			{
				node["rotation"][3], // w
				node["rotation"][0], // x
				node["rotation"][1], // y
				node["rotation"][2]  // z
			};
			nodeRotation = glm::make_quat(rotValues);
		}

		glm::vec3 nodeScale = glm::vec3(1.0f, 1.0f, 1.0f);
		if (node.find("scale") != node.end())
		{
			float scaleValues[3];
			for (unsigned int i = 0; i < node["scale"].size(); i++)
				scaleValues[i] = (node["scale"][i]);
			nodeScale = glm::make_vec3(scaleValues);
		}

		glm::mat4 matThisNode = glm::mat4(1.0f);
		if (node.find("matrix") != node.end())
		{
			float matValues[16];
			for (unsigned int i = 0; i < node["matrix"].size(); i++)
				matValues[i] = (node["matrix"][i]);
			matThisNode = glm::make_mat4(matValues);
		}

		glm::mat4 translation = glm::mat4(1.0f);
		glm::mat4 rotate = glm::mat4(1.0f);
		glm::mat4 scale = glm::mat4(1.0f);

		translation = glm::translate(translation, nodeTranslation);
		rotate = glm::mat4_cast(nodeRotation);
		scale = glm::scale(scale, nodeScale);

		glm::mat4 matNextNode = matrix * matThisNode * translation * rotate * scale;

		if (node.find("mesh") != node.end())
		{
			m_MatricesMeshes.push_back(matNextNode);
			loadMesh(node["mesh"], matNextNode);
		}

		// Check if the node has children
		if (node.find("children") != node.end())
		{
			for (unsigned int i = 0; i < node["children"].size(); i++)
				traverseNode(node["children"][i], matNextNode);
		}
	}

	void gltfModel::loadMesh(unsigned int meshIndex, glm::mat4 matrix /*= glm::mat4(1.0f)*/)
	{
		for (uint32_t i = 0; i < JSON["meshes"][meshIndex]["primitives"].size(); i++)
		{
			int meshPosIndex = JSON["meshes"][meshIndex]["primitives"][i]["attributes"]["POSITION"];
			int meshNormalIndex = JSON["meshes"][meshIndex]["primitives"][i]["attributes"].value("NORMAL", -1);
			int meshTexIndex = JSON["meshes"][meshIndex]["primitives"][i]["attributes"].value("TEXCOORD_0", -1);
			int meshIndicesIndex = JSON["meshes"][meshIndex]["primitives"][i]["indices"];
			int meshMaterialIndex = JSON["meshes"][meshIndex]["primitives"][i]["material"];

			// Get Pos mesh data
			std::vector<glm::vec3> position;
			std::vector<float> posRaw = getFloats(JSON["accessors"][meshPosIndex]);
			for (uint32_t j = 0; j < posRaw.size() / 3; j++)
				position.push_back(glm::vec3(posRaw[(j * 3)], posRaw[(j * 3) + 1], posRaw[(j * 3) + 2]));
			
			// Get Color mesh data
			std::vector<glm::vec3> colors;
			glm::vec3 colorRaw = getColors(JSON["materials"][meshMaterialIndex]);
			for (uint32_t j = 0; j < position.size(); j++)
				colors.push_back(colorRaw);

			// Get Normal mesh data
			std::vector<glm::vec3> normals;
			if (meshNormalIndex >= 0)
			{
				std::vector<float> normalsRaw = getFloats(JSON["accessors"][meshNormalIndex]);
				for (uint32_t j = 0; j < normalsRaw.size() / 3; j++)
					normals.push_back(glm::vec3(normalsRaw[(j * 3)], normalsRaw[(j * 3) + 1], normalsRaw[(j * 3) + 2]));
			}
			else // Mesh has no normals
			{
				for (uint32_t j = 0; j < position.size(); j++)
					normals.push_back(glm::vec3(0.0f));
			}

			// Get texUV mesh data
			std::vector<glm::vec2> texCoord;
			if (meshTexIndex >= 0)
			{
				std::vector<float> texCoordRaw = getFloats(JSON["accessors"][meshTexIndex]);
				for (int j = 0; j < texCoordRaw.size() / 2; j++)
					texCoord.push_back(glm::vec2(texCoordRaw[(j * 2)], texCoordRaw[(j * 2) + 1]));
			}
			else
			{
				for (uint32_t j = 0; j < position.size(); j++)
					texCoord.push_back(glm::vec2(0.0f, 0.0f));
			}

			// Get Mesh Indices
			std::vector<uint32_t> indices;
			std::vector<uint32_t> indicesRaw = getIndices(JSON["accessors"][meshIndicesIndex]);
			indices.insert(indices.end(), indicesRaw.begin(), indicesRaw.end());

			// Combine all mesh data and Get Model data
			std::vector<vertex> vertices;
			for (int j = 0; j < position.size(); j++)
			{
				vertices.emplace_back(
					vertex
					{
						position[j],
						colors[j],
						texCoord[j],
						normals[j],
					}
				);
			}

			// Get Textures
			std::vector<Texture*> textures = getTextures(JSON["materials"][meshMaterialIndex]);

			// Create Mesh
			Mesh mesh = Mesh(vertices, indices, textures);
			mesh.matrix = matrix;
			m_Meshes.emplace_back(std::move(mesh));
		}
	}

	std::vector<unsigned char> gltfModel::getData()
	{
		std::string bytesText;
		std::string uri = JSON["buffers"][0]["uri"];

		std::string fileStr = std::string(m_File);
		std::string fileDirectory = fileStr.substr(0, fileStr.find_last_of("/\\") + 1);
		bytesText = get_file_contents((fileDirectory + uri).c_str());

		std::vector<unsigned char> data(bytesText.begin(), bytesText.end());
		return data;
	}

	std::vector<float> gltfModel::getFloats(json accessor)
	{
		std::vector<float> floatData;

		unsigned int bufferViewIndex = accessor.value("bufferView", 0);
		unsigned int count = accessor["count"];
		unsigned int accByteOffset = accessor.value("byteOffset", 0);
		std::string type = accessor["type"];

		json bufferView = JSON["bufferViews"][bufferViewIndex];
		unsigned int BVbyteOffset = bufferView.value("byteOffset", 0);

		unsigned int typeNum = 0;
		if (type == "SCALAR") typeNum = 1;
		else if (type == "VEC2") typeNum = 2;
		else if (type == "VEC3") typeNum = 3;
		else if (type == "VEC4") typeNum = 4;
		else
			CHONPS_CORE_LOG_ERROR(Model:gltf:json, "Unkown float type! Type does not match with one of the following: VEC2 VEC3 VEC4 SCALER");

		unsigned int begginingOfData = accByteOffset + BVbyteOffset;
		unsigned int lengthOfData = count * sizeof(float) * typeNum;

		for (unsigned int i = begginingOfData; i < begginingOfData + lengthOfData; i += 4)
		{
			unsigned char bytes[] = { m_Data[i], m_Data[i + 1], m_Data[i + 2], m_Data[i + 3] };
			float value;
			std::memcpy(&value, bytes, sizeof(float));
			floatData.push_back(value);
		}

		return floatData;
	}

	glm::vec3 gltfModel::getColors(json material)
	{
		glm::vec3 colors = glm::vec3(1.0f);

		if (material["pbrMetallicRoughness"].find("baseColorFactor") != material["pbrMetallicRoughness"].end())
		{
			colors = glm::vec3(material["pbrMetallicRoughness"]["baseColorFactor"][0], material["pbrMetallicRoughness"]["baseColorFactor"][1], material["pbrMetallicRoughness"]["baseColorFactor"][2]);
			return colors;
		}
		return colors;
	}

	std::vector<uint32_t> gltfModel::getIndices(json accessor)
	{
		std::vector<uint32_t> indices;

		unsigned int bufferViewIndex = accessor.value("bufferView", 0);
		unsigned int count = accessor["count"];
		unsigned int accByteOffset = accessor.value("byteOffset", 0);
		unsigned int componentType = accessor["componentType"];

		json bufferView = JSON["bufferViews"][bufferViewIndex];
		unsigned int BVbyteOffset = bufferView.value("byteOffset", 0);

		unsigned int beginningOfData = BVbyteOffset + accByteOffset;

		if (componentType == 5125) // UNSIGNED_INT = 5125
		{
			for (unsigned int i = beginningOfData; i < BVbyteOffset + accByteOffset + count * sizeof(unsigned int); i += 4)
			{
				unsigned char bytes[] = { m_Data[i], m_Data[i + 1], m_Data[i + 2], m_Data[i + 3] };
				unsigned int value;
				std::memcpy(&value, bytes, sizeof(unsigned int));
				indices.push_back((uint32_t)value);
			}
		}
		else if (componentType == 5123) // UNSIGNED_SHORT = 5123
		{
			for (unsigned int i = beginningOfData; i < BVbyteOffset + accByteOffset + count * sizeof(unsigned short); i += 2)
			{
				unsigned char bytes[] = { m_Data[i], m_Data[i + 1] };
				unsigned short value;
				std::memcpy(&value, bytes, sizeof(unsigned short));
				indices.push_back((uint32_t)value);
			}
		}
		else if (componentType == 5122) // SHORT = 5122
		{
			for (unsigned int i = beginningOfData; i < BVbyteOffset + accByteOffset + count * sizeof(short); i += 2)
			{
				unsigned char bytes[] = { m_Data[i], m_Data[i + 1] };
				short value;
				std::memcpy(&value, bytes, sizeof(short));
				indices.push_back((uint32_t)value);
			}
		}
		else
			CHONPS_CORE_LOG_ERROR(Model:gltf:json, "Unkown component type! Component type does not match with one of the following:\n'5125' (UNSIGNED_INT)\n'5123' (UNSIGNED_SHORT)\n'5122' (SHORT)\n");

		return indices;
	}

	std::vector<Texture*> gltfModel::getTextures(json material)
	{
		std::vector<Texture*> textures;

		std::string fileString = std::string(m_File);
		std::string fileDirectory = fileString.substr(0, fileString.find_last_of("/\\") + 1);

		unsigned int texSource;
		std::string texPath;

		if (material["pbrMetallicRoughness"].find("baseColorTexture") != material["pbrMetallicRoughness"].end()) // Diffuse 
		{
			json texInd = JSON["textures"][(unsigned int)material["pbrMetallicRoughness"]["baseColorTexture"]["index"]];
			texSource = texInd["source"];
			texPath = JSON["images"][texSource]["uri"];

			bool skip = false;
			for (unsigned int i = 0; i < m_TexIndex.size(); i++)
			{
				if (m_TexIndex[i] == texSource)
				{
					textures.emplace_back(m_Textures[i]);
					skip = true;
					break;
				}
			}
			if (!skip)
			{
				unsigned int texSamplerIndex = texInd["sampler"];
				uint32_t magFilter = JSON["samplers"][texSamplerIndex]["magFilter"];
				uint32_t minFilter = JSON["samplers"][texSamplerIndex]["minFilter"];

				Texture* texture = createTexture((fileDirectory + texPath).c_str(), TexType::Albedo, GetTexFilter(magFilter, minFilter));
				textures.emplace_back(texture);
				m_Textures.emplace_back(&(*texture));
				m_TexIndex.push_back(texSource);
			}
		}
		else // White Texture if no Diffuse Texture was found
		{
			uint32_t whiteTextureData = 0xffffffff;
			Texture* texture = createTexture(1, 1, 4, &whiteTextureData);
			textures.emplace_back(texture);
		}
		if (material["pbrMetallicRoughness"].find("metallicRoughnessTexture") != material["pbrMetallicRoughness"].end()) // Metallic Roughness
		{
			json texInd = JSON["textures"][(unsigned int)material["pbrMetallicRoughness"]["metallicRoughnessTexture"]["index"]];
			texSource = texInd["source"];
			texPath = JSON["images"][texSource]["uri"];

			bool skip = false;
			for (unsigned int i = 0; i < m_TexIndex.size(); i++)
			{
				if (m_TexIndex[i] == texSource)
				{
					textures.emplace_back(m_Textures[i]);
					skip = true;
					break;
				}
			}
			if (!skip)
			{
				unsigned int texSamplerIndex = texInd["sampler"];
				uint32_t magFilter = JSON["samplers"][texSamplerIndex]["magFilter"];
				uint32_t minFilter = JSON["samplers"][texSamplerIndex]["minFilter"];

				Texture* texture = createTexture((fileDirectory + texPath).c_str(), TexType::MetallicRoughness, GetTexFilter(magFilter, minFilter));
				textures.emplace_back(texture);
				m_Textures.emplace_back(&(*texture));
				m_TexIndex.push_back(texSource);
			}
		}
		if (material.find("normalTexture") != material.end()) // Normal
		{
			json texInd = JSON["textures"][(unsigned int)material["normalTexture"]["index"]];
			texSource = texInd["source"];
			texPath = JSON["images"][texSource]["uri"];

			bool skip = false;
			for (unsigned int i = 0; i < m_TexIndex.size(); i++)
			{
				if (m_TexIndex[i] == texSource)
				{
					textures.emplace_back(m_Textures[i]);
					skip = true;
					break;
				}
			}
			if (!skip)
			{
				unsigned int texSamplerIndex = texInd["sampler"];
				uint32_t magFilter = JSON["samplers"][texSamplerIndex]["magFilter"];
				uint32_t minFilter = JSON["samplers"][texSamplerIndex]["minFilter"];

				Texture* texture = createTexture((fileDirectory + texPath).c_str(), TexType::Normal, GetTexFilter(magFilter, minFilter));
				textures.emplace_back(texture);
				m_Textures.emplace_back(&(*texture));
				m_TexIndex.push_back(texSource);
			}
		}
		if (material.find("occlusionTexture") != material.end()) // Occlusion
		{
			json texInd = JSON["textures"][(unsigned int)material["occlusionTexture"]["index"]];
			texSource = texInd["source"];
			texPath = JSON["images"][texSource]["uri"];

			bool skip = false;
			for (unsigned int i = 0; i < m_TexIndex.size(); i++)
			{
				if (m_TexIndex[i] == texSource)
				{
					textures.emplace_back(m_Textures[i]);
					skip = true;
					break;
				}
			}
			if (!skip)
			{
				unsigned int texSamplerIndex = texInd["sampler"];
				uint32_t magFilter = JSON["samplers"][texSamplerIndex]["magFilter"];
				uint32_t minFilter = JSON["samplers"][texSamplerIndex]["minFilter"];

				Texture* texture = createTexture((fileDirectory + texPath).c_str(), TexType::Occlusion, GetTexFilter(magFilter, minFilter));
				textures.emplace_back(texture);
				m_Textures.emplace_back(&(*texture));
				m_TexIndex.push_back(texSource);
			}
		}
		if (material.find("emissiveTexture") != material.end()) // Emissive
		{
			json texInd = JSON["textures"][(unsigned int)material["emissiveTexture"]["index"]];
			texSource = texInd["source"];
			texPath = JSON["images"][texSource]["uri"];

			bool skip = false;
			for (unsigned int i = 0; i < m_TexIndex.size(); i++)
			{
				if (m_TexIndex[i] == texSource)
				{
					textures.emplace_back(m_Textures[i]);
					skip = true;
					break;
				}
			}
			if (!skip)
			{
				unsigned int texSamplerIndex = texInd["sampler"];
				uint32_t magFilter = JSON["samplers"][texSamplerIndex]["magFilter"];
				uint32_t minFilter = JSON["samplers"][texSamplerIndex]["minFilter"];

				Texture* texture = createTexture((fileDirectory + texPath).c_str(), TexType::Emissive, GetTexFilter(magFilter, minFilter));
				textures.emplace_back(texture);
				m_Textures.emplace_back(&(*texture));
				m_TexIndex.push_back(texSource);
			}
		}

		return textures;
	}

	TexFilterPair gltfModel::GetTexFilter(uint32_t mag, uint32_t min)
	{
		TexFilter magTexFilter = TexFilter::Nearest, minTexFilter = TexFilter::Linear;
		mag == 9728 ? magTexFilter = TexFilter::Nearest : magTexFilter = TexFilter::Linear;

		if (min == 9728) minTexFilter = TexFilter::Nearest;
		else if (min == 9729) minTexFilter = TexFilter::Linear;
		else if (min == 9984) minTexFilter = TexFilter::Nearest_Mipmap_Nearest;
		else if (min == 9985) minTexFilter = TexFilter::Linear_Mipmap_Nearest;
		else if (min == 9986) minTexFilter = TexFilter::Nearest_Mipmap_Linear;
		else if (min == 9987) minTexFilter = TexFilter::Linear_Mipmap_Linear;

		TexFilterPair filterPair;
		filterPair.mag = magTexFilter;
		filterPair.min = minTexFilter;

		return filterPair;
	}

	TexWrap gltfModel::GetTexWrap(uint32_t wrap)
	{
		return TexWrap();
	}
}