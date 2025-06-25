#include <cstdlib>
#include <cstdio>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>
#define MAX_OBJ_LEN 1024 * 1000

namespace Util
{
#define SHADER_MAX 1024 * 10

	bool LoadObj(const char* filename, std::vector<float>* verts, std::vector<unsigned int>* inds)
	{
		std::vector<float> vertices;
		std::vector<uint32_t> indices;

		// read the file
		FILE* fp = fopen(filename, "r");
		if (!fp)
			return false;

		char* buff = new char[MAX_OBJ_LEN];

		int numRead = fread(buff, 1, MAX_OBJ_LEN, fp);
		// assuming numRead << MAX_OBJ_LEN
		buff[numRead] = '\n';

		fclose(fp);

		char vBuff[20]; // the string for the vertices
		char iBuff[10]; // the string for the indices

		// read each line
		int lineStart = 0;
		int lineEnd = 0;
		for (int i = 0; i <= numRead; i++)
		{
			// if we have encountered a new line
			// process the line, split it into components then convert those into verts or inds
			if (buff[i] == '\n')
			{
				lineEnd = i;
				// if the line is a vertex
				if (buff[lineStart] == 'v')
				{
					int vStart = lineStart+1;
					int vEnd = vStart;
					for (; vEnd <= lineEnd; ++vEnd)
					{
						if (vEnd > vStart && buff[vEnd] == ' ' || buff[vEnd] == '\n')
						{
							// copy the float string and convert it to a float using atof
							memcpy(vBuff, buff+vStart+1, vEnd - vStart - 1);
							vBuff[vEnd-vStart] = '\0';
							vertices.push_back(atof(vBuff));
							vStart = vEnd;
							memset(vBuff, 0, sizeof(vBuff));
						}
					}

				}
				// if the line is an index
				else if (buff[lineStart] == 'f')
				{
					int iStart = lineStart+1;
					int iEnd = iStart;
					for (; iEnd <= lineEnd; ++iEnd) 
					{
						if (iEnd > iStart && buff[iEnd] == ' ' || buff[iEnd] == '\n')
						{
							// copy the int string and convert it to a int using atoi
							memcpy(iBuff, buff+iStart+1, iEnd - iStart - 1);
							iBuff[iEnd-iStart] = '\0';
							indices.push_back(atoi(iBuff) - 1);
							iStart = iEnd;
							memset(iBuff, 0, sizeof(iBuff));
						}
					}
				}

				lineStart = lineEnd = (i+1);
			}
		}
		*verts = vertices;
		*inds = indices;
		delete[] buff;
		return true;
	}


	std::vector<float> GenerateNormals(const std::vector<float>& vertices, const std::vector<uint32_t>& indices)
	{
		std::map<uint32_t, std::vector<glm::vec3>> normals;

		// variables to store vectors temporarily
		glm::vec3 v1;
		glm::vec3 v2;
		glm::vec3 v3;
		// go through all the indices
		for (size_t i = 0; i < indices.size() - 2; i+=3)
		{
			uint32_t ind1 = 3*indices[i];
			uint32_t ind2 = 3*indices[i + 1];
			uint32_t ind3 = 3*indices[i + 2];
			v1.x = vertices[ind1];
			v1.y = vertices[ind1 + 1];
			v1.z = vertices[ind1 + 2];

			v2.x = vertices[ind2];
			v2.y = vertices[ind2 + 1];
			v2.z = vertices[ind2 + 2];

			v3.x = vertices[ind3];
			v3.y = vertices[ind3 + 1];
			v3.z = vertices[ind3 + 2];

			glm::vec3 v12 = v2 - v1;
			glm::vec3 v13 = v3 - v1;

			glm::vec3 normal = glm::cross(v12, v13);
			if (normals.count(ind1/3))
			{
				normals[ind1/3].push_back(normal);
			}
			else
			{
				normals[ind1/3] = std::vector<glm::vec3>();
				normals[ind1/3].push_back(normal);
			}
			if (normals.count(ind2/3))
			{
				normals[ind2/3].push_back(normal);
			}
			else
			{
				normals[ind2/3] = std::vector<glm::vec3>();
				normals[ind2/3].push_back(normal);
			}
			if (normals.count(ind3/3))
			{
				normals[ind3/3].push_back(normal);
			}
			else
			{
				normals[ind3/3] = std::vector<glm::vec3>();
				normals[ind3/3].push_back(normal);
			}
		}


		// go through all the verts and normals

		std::vector<float> out;
		uint32_t ind = 0;
		for (size_t i = 0; i < vertices.size() - 2; i+=3, ind++)
		{
			out.push_back(vertices[i]);
			out.push_back(vertices[i + 1]);
			out.push_back(vertices[i + 2]);
			glm::vec3 normal(0);
			std::vector<glm::vec3> ns = normals[ind];
			for (auto n : ns)
			{
				normal += n;
			}
			out.push_back(normal.x);
			out.push_back(normal.y);
			out.push_back(normal.z);
		}
		return out;
	}
	class Shader
	{
	public:
		bool LoadFromFile(const char* vss, const char* fss)
		{
			unsigned int vs = LoadShader(vss, GL_VERTEX_SHADER);
			if (!vs)
			{
				std::cout << "Failed to load vertex shader\n";
				return false;
			}
			unsigned int fs = LoadShader(fss, GL_FRAGMENT_SHADER);
			if (!fs)
			{
				std::cout << "Failed to load fragment shader\n";
				return false;
			}

			mId = glCreateProgram();
			glAttachShader(mId, vs);
			glAttachShader(mId, fs);
			glLinkProgram(mId);

			int success;
			glGetProgramiv(mId, GL_LINK_STATUS, &success);
			if (!success)
			{
				char infoLog[512];
				glGetProgramInfoLog(mId, 512, nullptr, infoLog);
				std::cout << infoLog << std::endl;
				return false;
			}
			glDeleteShader(vs);
			glDeleteShader(fs);
			return true;
		}

		void Use() const
		{
			glUseProgram(mId);
		}

		void SetMat4(const glm::mat4& mat, const char* name) const
		{
			unsigned int loc = glGetUniformLocation(mId, name);
			Use();
			glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mat));
		}

		void SetVec3(const glm::vec3& vec, const char* name) const
		{
			unsigned int loc = glGetUniformLocation(mId, name);
			Use();
			glUniform3fv(loc, 1, glm::value_ptr(vec));
		}
		
		void SetVec4(const glm::vec4& vec, const char* name) const
		{
			unsigned int loc = glGetUniformLocation(mId, name);
			Use();
			glUniform4fv(loc, 1, glm::value_ptr(vec));
		}

	private:

		unsigned int LoadShader(const char* filename, GLenum type)
		{
			unsigned int shader = glCreateShader(type);
			FILE* fp = fopen(filename, "r");
			if (!fp)
				return 0;
			char src[SHADER_MAX];
			int numRead = fread(src, 1, SHADER_MAX, fp);
			src[numRead] = '\0';
			fclose(fp);

			const char* source = (const char*)src;
			glShaderSource(shader, 1, &source, nullptr);
			glCompileShader(shader);

			int success;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
			if (!success)
			{
				char infoLog[512];
				glGetShaderInfoLog(shader, 512, nullptr, infoLog);
				std::cout << infoLog << std::endl;
				return 0;
			}
			return shader;
		}

	private:
		unsigned int mId;
	};

	class Model
	{

	public:
		Model()
		{
			mPosition = glm::vec3(0);
			mRotation = glm::vec3(0);
		}
		bool LoadFromFile(const char* filename)
		{
			if (!LoadObj(filename, &mVerts, &mInds))
				return false;
			mVerts = GenerateNormals(mVerts, mInds);

			UploadBufferData();
			return true;
		}

		void LoadFromMemory(const std::vector<float>& verts, const std::vector<unsigned int>& inds)
		{
			mVerts = verts;
			mInds = inds;
			mVerts = GenerateNormals(mVerts, mInds);
			UploadBufferData();
		}

		void Render(const Shader& shader)
		{
			auto model = GetGlobalTransform();
			shader.SetMat4(model, "modelMat");
			glBindVertexArray(vao);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
			glDrawElements(GL_TRIANGLES, mInds.size(), GL_UNSIGNED_INT, nullptr);
		}

	private:
		void UploadBufferData()
		{
			glGenVertexArrays(1, &vao);
			glBindVertexArray(vao);
			
			glGenBuffers(1, &vbo);
			glGenBuffers(1, &ebo);

			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

			glBufferData(GL_ARRAY_BUFFER, mVerts.size() * 4, mVerts.data(), GL_STATIC_DRAW);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, mInds.size() * 4, mInds.data(), GL_STATIC_DRAW);

			// TODO: try sending the data here instead
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
			glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
			glEnableVertexAttribArray(0);
			glEnableVertexAttribArray(1);
		}

		glm::mat4 GetGlobalTransform()
		{
			glm::mat4 translation = glm::translate(glm::mat4(1), mPosition);
			glm::mat4 rotX = glm::rotate(glm::mat4(1), mRotation.x, glm::vec3(1, 0, 0));
			glm::mat4 rotY = glm::rotate(glm::mat4(1), mRotation.y, glm::vec3(0, 1, 0));
			glm::mat4 rotZ = glm::rotate(glm::mat4(1), mRotation.z, glm::vec3(0, 0, 1));
			return (translation * rotX * rotY * rotZ);
		}

	private:
		std::vector<float> mVerts;
		std::vector<unsigned int> mInds;
		unsigned int vao, vbo, ebo;
		glm::vec3 mPosition;
		glm::vec3 mRotation;
	};

}
