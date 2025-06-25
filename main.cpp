#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "util.h"
#include <vector>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string.h>
#include <random>
#include <chrono>
#include <thread>
#include <future>
#include <map>

#ifndef M_PI
#define M_PI 3.141592653589793
#endif

#if _WIN32
int randomSeed = 1;
int Random()
{
	randomSeed = randomSeed * 214013 + 2531011;
    	return (randomSeed >> 16) & 32767;
}

float drand48()
{
	return Random()/32767.0f;
}
#endif


using namespace Util;

typedef unsigned char pixel_t;

int w = 1200, h = 600;
const float T_MAX = 1000000.0f;
const float eps = 0.001;
const int MAX_DEPTH = 4;
int numSamples = 2048;
int numThreads = 2;
bool running = true;
const glm::vec4 black = glm::vec4(0);

struct Sphere;
struct Hit
{
	glm::vec3 normal;
	float t = T_MAX;
	const Sphere* sphere = nullptr;
	bool hit = false;
};

struct Ray
{
	glm::vec3 origin;
	glm::vec3 direction;

	glm::vec3 GetPointAt(float t) const
	{
		return (origin + t*direction);
	}
};

struct Sphere
{
	glm::vec4 color;
	glm::vec4 emission;
	glm::vec4 Fo = glm::vec4{0.91, 0.92, 0.92, 1};
	glm::vec3 center;
	float radius;
	float roughness = 0.5;
	float metallic = 0.0f;

	Hit GetHit(const Ray& ray) const
	{
		glm::vec3 oc = ray.origin - center;
		float a = glm::dot(ray.direction, ray.direction);
		float b = 2.0f * glm::dot(oc, ray.direction);
		float c = glm::dot(oc, oc) - pow(radius, 2);
		Hit hitInfo;

		float d = b*b - 4*a*c;
		if (d >= 0)
		{
			float t1 = (-b - sqrt(d))/(2*a);
			float t2 = (-b + sqrt(d))/(2*a);

			hitInfo.hit = true;
			float t = T_MAX;
			if (t1 < 0 && t2 >= 0)
				t = t2;
			else if (t1 >= 0 && t2 < 0)
				t = t1;
			else if (t1 >= 0 && t2 >= 0)
				t = glm::min(t1, t2);
			else
				hitInfo.hit = false;
			hitInfo.t = t;
		}
		else
			hitInfo.hit = false;
		return hitInfo;
	}

	glm::vec3 GetNormal(glm::vec3 point) const
	{
		return glm::normalize(point - center);
	}
};

struct Camera
{
	glm::vec3 position;
	glm::vec3 lookAt;
	glm::vec3 up;
	
	std::map<std::pair<int, int>, Ray> cachedRays;
	
	float fov;
	float aspect_ratio;

	Ray GetRay(float u, float v) const
	{
		u = 2*u - 1;
		v = 2*v - 1;

		glm::vec3 right = glm::normalize(glm::cross(lookAt, up));
		float flen = aspect_ratio/(2 * tanf(fov));
		Ray ray;
		glm::vec3 pixelPos = right * u * aspect_ratio + up * v + lookAt * flen;
		ray.origin = position;
		ray.direction = pixelPos - position;
		return ray;
	}

	void CacheRay(Ray ray, int x, int y)
	{
		cachedRays[std::pair<int, int>(x, y)] = ray;
	}

	Ray GetCachedRay(int x, int y)
	{
		return cachedRays[std::pair<int, int>(x, y)];
	}
};

struct Scene
{
	std::vector<Sphere> spheres;
	Camera mainCamera;
};

void SetAt(pixel_t* pixels, int x, int y, pixel_t r, pixel_t g, pixel_t b, pixel_t a=255)
{
	int offset = 4*(w*y + x);
	pixels[offset] = r;
	pixels[offset + 1] = g;
	pixels[offset + 2] = b;
	pixels[offset + 3] = a;
}

void SetAt(pixel_t* pixels, int x, int y, glm::vec4 color)
{
	pixel_t r = glm::clamp(255.0f * color.r, 0.0f, 255.0f);
	pixel_t g = glm::clamp(255.0f * color.g, 0.0f, 255.0f);
	pixel_t b = glm::clamp(255.0f * color.b, 0.0f, 255.0f);
	pixel_t a = glm::clamp(255.0f * color.a, 0.0f, 255.0f);
	SetAt(pixels, x, y, r, g, b, a);
}
glm::vec4 GetAt(pixel_t* pixels, int x, int y)
{
	int offset = 4*(w*y + x);
	glm::vec4 color;
	color.x = pixels[offset];
	color.y = pixels[offset + 1];
	color.z = pixels[offset + 2];
	color.w = pixels[offset + 3];
	color /= 255.0f;
	return color;
}

Hit GetClosest(const Ray& ray, const std::vector<Sphere>& spheres)
{
	Hit hit;

	for (auto& sphere : spheres)
	{
		Hit h = sphere.GetHit(ray);
		if (h.hit && h.t < hit.t)
		{
			hit.hit = true;
			hit.t = h.t;
			hit.sphere = &sphere;
			hit.normal = sphere.GetNormal(ray.GetPointAt(hit.t));
		}
	}
	return hit;
}

float RandomSample(float a = -1, float b = 1)
{
	float t = drand48();
	return (1.0f - 2.0f * t);
}

glm::vec3 GetRandomDirection(glm::vec3 normal)
{
	glm::vec3 dir;
	do
	{
		dir = glm::vec3(RandomSample(), RandomSample(), RandomSample());
	} while (glm::dot(normal, dir) < 0);
	return dir;
}

// the brdf functions

float D(glm::vec3 n, glm::vec3 h, float a)
{
	float a2 = a*a;
	float denom = (float)M_PI*pow((pow(glm::dot(n, h), 2)*(a2 - 1) + 1), 2);
	return a2/denom;
}

float Gp(glm::vec3 n, glm::vec3 v, float k)
{
	float ndotv = glm::dot(n, v);
	float denom = ndotv*(1 - k) + k;
	return ndotv/denom;
}

float G(glm::vec3 n, glm::vec3 v, glm::vec3 l, float a)
{
	float k = a*a/2.0f;
	return Gp(n, v, k)*Gp(n, l, k);
}

glm::vec4 F(glm::vec3 h, glm::vec3 v, glm::vec4 Fo)
{
	return Fo + (1.0f - Fo)*(float)pow(1.0f - glm::dot(h, v), 5);
}

glm::vec4 Raytrace(const Ray& ray, const std::vector<Sphere>& spheres, int depth=0)
{
	if (depth >= MAX_DEPTH)
		return black;
	
	Hit hit = GetClosest(ray, spheres);
	if (!hit.hit)
		return black;

	Ray newRay;
	newRay.origin = hit.normal * eps + ray.GetPointAt(hit.t);
	newRay.direction = GetRandomDirection(hit.normal);

	glm::vec4 incoming = Raytrace(newRay, spheres, ++depth);
	glm::vec4 diffuse = hit.sphere->color/(float)M_PI;
	float ndotl = glm::dot(hit.normal, newRay.direction);

	auto n = hit.normal;
	auto l = newRay.direction;
	auto v = glm::normalize(-ray.direction);
	auto h = glm::normalize(v + l);
	float a = hit.sphere->roughness;
	float m = hit.sphere->metallic;
	auto Fo = glm::mix(hit.sphere->Fo, hit.sphere->color, m);
	
	auto DFG = D(n, h, a) * F(h, v, Fo) * G(n, v, l, a);
	auto ct = DFG/(4.0f * glm::dot(v, n) * glm::dot(l, n));
	auto kd = 1.0f - ct;
	kd *= 1.0f - m;
	/* std::cout << DFG.x << " " << DFG.y << " " << DFG.z << std::endl; */


	return (hit.sphere->emission + (kd*diffuse + ct) * incoming * ndotl);
}

glm::vec4 PerPixel(Scene& scene, pixel_t* pixels, int x, int y)
{

	/* float u = (float)x/w; */
	/* float v = (float)y/h; */

	/* Ray ray = scene.mainCamera.GetRay(u, v); */

	Ray ray = scene.mainCamera.GetCachedRay(x, y);
	glm::vec4 color = Raytrace(ray, scene.spheres);

	/* SetAt(pixels, x, y, color); */
	return color;
}

int Render(Scene& scene, pixel_t* pixels, int startX)
{
	for (int i = 0; running && (i < numSamples); i++)
	{
		for (int y = 0; running && (y < h); y++)
		{
			for (int x = startX; running && (x < w); x += numThreads)
			{
				if (i == 0)
				{
					float u = (float)x/w;
					float v = (float)y/h;

					Ray ray = scene.mainCamera.GetRay(u, v);
					scene.mainCamera.CacheRay(ray, x, y);
				}

				glm::vec4 color = black;

				color = GetAt(pixels, x, y);

				color.x = pow(color.x, 2.2f);
				color.y = pow(color.y, 2.2f);
				color.z = pow(color.z, 2.2f);
				color *= (float)i;

				color += PerPixel(scene, pixels, x, y);

				color /= (float)(i + 1);
				color.x = pow(color.x, 1/2.2f);
				color.y = pow(color.y, 1/2.2f);
				color.z = pow(color.z, 1/2.2f);
				SetAt(pixels, x, y, color);
			}

			/* float per = (float)y/h * 100.0f; */
			/* std::cout << (startX + 1) << " > " << (per + 1) << "%\n"; */
		}
	}
	return 0;
}

int main()
{
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	GLFWwindow* window = glfwCreateWindow(w, h, "Raytracing", nullptr, nullptr);
	glfwMakeContextCurrent(window);

	glewInit();

	std::vector<float> verts = {
		-1, 1, 0, 0, 1,
		1, 1, 0, 1, 1,
		1, -1, 0, 1, 0,
		-1, -1, 0, 0, 0
	};

	std::vector<unsigned int> inds = {
		0, 3, 1,
		1, 3, 2
	};

	unsigned int vao, vbo, ebo;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ebo);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

	glBufferData(GL_ARRAY_BUFFER, verts.size() * 4, verts.data(), GL_STATIC_DRAW);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, inds.size() * 4, inds.data(), GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, nullptr);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (void*)12);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	Shader shader;

	if (!shader.LoadFromFile("assets/Vertex.shader", "assets/Fragment.shader"))
	{
		std::cout << "Failed to load shaders\n";
	}

	pixel_t* pixels = new pixel_t[w * h * 4];

	Scene scene;

	Camera camera;
	camera.position = glm::vec3(0, 0, 14);
	camera.lookAt = glm::vec3(0, 0, -1);
	camera.up = glm::vec3(0, 1, 0);
	camera.fov = M_PI/6.0f;
	camera.aspect_ratio = (float)w/h;

	// left wall
	Sphere sphere;
	sphere.center = glm::vec3(-1001.5, 0, 0);
	sphere.color = glm::vec4(1, 0, 0, 1);
	sphere.radius = 1000;
	sphere.emission = glm::vec4(0);
	scene.spheres.push_back(sphere);

	// right wall
	sphere.center = glm::vec3(1001.5, 0, 0);
	sphere.color = glm::vec4(0, 1, 0, 1);
	sphere.radius = 1000;
	sphere.emission = glm::vec4(0);
	scene.spheres.push_back(sphere);

	// center wall
	sphere.center = glm::vec3(0, 0, -1001.5);
	sphere.color = glm::vec4(0, 0, 1, 1);
	sphere.radius = 1000;
	sphere.emission = glm::vec4(0);
	scene.spheres.push_back(sphere);

	// top wall(light)
	sphere.center = glm::vec3(0, 1001.5, 0);
	sphere.color = glm::vec4(0, 0, 0, 1);
	sphere.radius = 1000;
	sphere.emission = glm::vec4(1);
	scene.spheres.push_back(sphere);

	// bottom wall
	sphere.center = glm::vec3(0, -1001 + 0.15, 0);
	sphere.color = glm::vec4(0.5, 0.5, 0.5, 1);
	sphere.radius = 1000;
	sphere.emission = glm::vec4(0);
	scene.spheres.push_back(sphere);

	// the three spheres
	sphere.center = glm::vec3(-1, -0.5 + 0.15, 0);
	sphere.color = glm::vec4(0.9, 0.2, 0.1, 1);
	sphere.radius = 0.5;
	sphere.emission = glm::vec4(0);
	sphere.metallic = 0;
	sphere.roughness = 0.9;
	sphere.Fo = glm::vec4(0.95, 0.64, 0.54, 1);
	scene.spheres.push_back(sphere);

	sphere.center = glm::vec3(0, -0.5 + 0.15, 0);
	sphere.color = glm::vec4(1, 1, 1, 1);
	sphere.radius = 0.5;
	sphere.emission = glm::vec4(0);
	sphere.roughness = 0.1f;
	sphere.Fo = glm::vec4{0.91, 0.92, 0.92, 1};
	sphere.metallic = 1;
	scene.spheres.push_back(sphere);

	sphere.center = glm::vec3(1, -0.5 + 0.15, 0);
	sphere.color = glm::vec4(1, 0, 0, 1);
	sphere.radius = 0.5;
	sphere.emission = glm::vec4(0);
	sphere.roughness = 0.1f;
	sphere.Fo = glm::vec4(0.03, 0.03, 0.03, 0.03);
	sphere.metallic = 0;
	scene.spheres.push_back(sphere);


	scene.mainCamera = camera;

	std::vector<std::future<int>> threads;
	for (int i = 0; i < numThreads; i++)
	{
		threads.push_back(std::async(Render, std::ref(scene), pixels, i));
	}

	/* auto startTime = std::chrono::high_resolution_clock::now(); */
	/* Render(scene, pixels, 0); */
	/* t1.get(); */
	/* auto endTime = std::chrono::high_resolution_clock::now(); */
	/* auto timeDiff = endTime - startTime; */

	/* float s = timeDiff/std::chrono::seconds(1); */
	/* std::cout << "Took " << s << "s\n"; */
	unsigned int tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);


	while(!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		glClear(GL_COLOR_BUFFER_BIT);
		shader.Use();
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		glDrawElements(GL_TRIANGLES, inds.size(), GL_UNSIGNED_INT, nullptr);
		glfwSwapBuffers(window);
	}

	running = false;
	for (int i = 0; i < threads.size(); i++)
	{
		threads[i].wait();
	}

	delete pixels;
	glfwDestroyWindow(window);
	glfwTerminate();
}
