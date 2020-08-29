#include <visii/texture.h>

#include <stb_image.h>
#include <stb_image_write.h>
#include <cstring>

Texture Texture::textures[MAX_TEXTURES];
TextureStruct Texture::textureStructs[MAX_TEXTURES];
std::map<std::string, uint32_t> Texture::lookupTable;
std::shared_ptr<std::mutex> Texture::editMutex;
bool Texture::factoryInitialized = false;
bool Texture::anyDirty = true;

Texture::Texture()
{
    this->initialized = false;
    markDirty();
}

Texture::~Texture()
{
    this->texels.clear();
}

Texture::Texture(std::string name, uint32_t id)
{
    this->initialized = true;
    this->name = name;
    this->id = id;

    // ------------------------------------------------------------------
    // create a 4x4 checkerboard texture
    // ------------------------------------------------------------------
    glm::ivec2 texSize = glm::ivec2(4);
    std::vector<vec4> texels;
    for (int iy=0;iy<texSize.y;iy++) {
        for (int ix=0;ix<texSize.x;ix++) {
            texels.push_back(((ix ^ iy)&1) ?
                            vec4(0,1,0,0) :
                            vec4(1));
        }
    }

    textureStructs[id].width = 4;
    textureStructs[id].height = 4;
    this->texels = texels;
    
    markDirty();
}

std::string Texture::toString() {
    std::string output;
    output += "{\n";
    output += "\ttype: \"Texture\",\n";
    output += "\tname: \"" + name + "\"\n";
    output += "}";
    return output;
}

std::vector<vec4> Texture::getTexels() {
    return texels;
}

uint32_t Texture::getWidth() {
    return textureStructs[id].width;
}

uint32_t Texture::getHeight() {
    return textureStructs[id].height;
}

/* SSBO logic */
void Texture::initializeFactory()
{
    if (isFactoryInitialized()) return;
    editMutex = std::make_shared<std::mutex>();
    factoryInitialized = true;
}

bool Texture::isFactoryInitialized()
{
    return factoryInitialized;
}

bool Texture::isInitialized()
{
	return initialized;
}

bool Texture::areAnyDirty()
{
	return anyDirty;
}

void Texture::markDirty() {
	dirty = true;
    anyDirty = true;
};

void Texture::updateComponents()
{
	if (!anyDirty) return;

	for (int i = 0; i < MAX_TEXTURES; ++i) {
		if (textures[i].isDirty()) {
            textures[i].markClean();
        }
	};
	anyDirty = false;
} 

void Texture::clearAll()
{
    if (!isFactoryInitialized()) return;

    for (auto &texture : textures) {
		if (texture.initialized) {
			Texture::remove(texture.name);
		}
	}
}	

/* Static Factory Implementations */
Texture* Texture::create(std::string name) {
    auto l = StaticFactory::create(editMutex, name, "Texture", lookupTable, textures, MAX_TEXTURES);
    anyDirty = true;
    return l;
}

Texture* Texture::createFromImage(std::string name, std::string path, bool linear) {
    auto create = [path, linear] (Texture* l) {
        int x, y, num_channels;
        stbi_set_flip_vertically_on_load(true);
        if (linear) {
            stbi_ldr_to_hdr_gamma(1.0f);
        } else {
            stbi_ldr_to_hdr_gamma(2.2f);
        }
        float* pixels = stbi_loadf(path.c_str(), &x, &y, &num_channels, STBI_rgb_alpha);
        if (!pixels) { 
            std::string reason (stbi_failure_reason());
            throw std::runtime_error(std::string("Error: failed to load texture image \"") + path + std::string("\". Reason: ") + reason); 
        }
        l->texels.resize(x * y);
        memcpy(l->texels.data(), pixels, x * y * 4 * sizeof(float));
        textureStructs[l->getId()].width = x;
        textureStructs[l->getId()].height = y;
        l->markDirty();
        stbi_image_free(pixels);
    };

    try {
        return StaticFactory::create<Texture>(editMutex, name, "Texture", lookupTable, textures, MAX_TEXTURES, create);
    } catch (...) {
		StaticFactory::removeIfExists(editMutex, name, "Texture", lookupTable, textures, MAX_TEXTURES);
		throw;
	}
}

Texture* Texture::createFromData(std::string name, uint32_t width, uint32_t height, std::vector<float> data)
{
    auto create = [width, height, &data] (Texture* l) {
        if (data.size() != (width * height * 4)) { throw std::runtime_error("Error: width * height * 4 does not equal length of data!"); }
        l->texels.resize(width * height);
        memcpy(l->texels.data(), data.data(), width * height * 4 * sizeof(float));
        textureStructs[l->getId()].width = width;
        textureStructs[l->getId()].height = height;
        l->markDirty();
    };

    try {
        return StaticFactory::create<Texture>(editMutex, name, "Texture", lookupTable, textures, MAX_TEXTURES, create);
    } catch (...) {
		StaticFactory::removeIfExists(editMutex, name, "Texture", lookupTable, textures, MAX_TEXTURES);
		throw;
	}
}

Texture* Texture::createHSV(std::string name, Texture* tex, float hue, float saturation, float value, float mix)
{
    auto create = [tex, hue, saturation, value, mix] (Texture* l) {
        if (!tex || !tex->isInitialized()) throw std::runtime_error(std::string("Error: input texture is null/uninitialized!")); 

        uint32_t width = tex->getWidth();
        uint32_t height = tex->getHeight();
        l->texels.resize(width * height);
        textureStructs[l->getId()].width = width;
        textureStructs[l->getId()].height = height;

        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                vec2 off = vec2(1 / float(width), 1 / float(height)); 
                vec2 uv = vec2(x / float(width), y / float(height)) + .5f * off;
                l->texels[y * width + x] = tex->sample(uv); // todo, transform in HSV space...
            }
        }
        
        l->markDirty();
    };

    try {
        return StaticFactory::create<Texture>(editMutex, name, "Texture", lookupTable, textures, MAX_TEXTURES, create);
    } catch (...) {
		StaticFactory::removeIfExists(editMutex, name, "Texture", lookupTable, textures, MAX_TEXTURES);
		throw;
	}
}

Texture* Texture::createMix(std::string name, Texture* a, Texture* b, float mix)
{
    auto create = [a, b, mix] (Texture* l) {
        if (!a || !a->isInitialized()) throw std::runtime_error(std::string("Error: Texture A is null/uninitialized!")); 
        if (!b || !b->isInitialized()) throw std::runtime_error(std::string("Error: Texture B is null/uninitialized!")); 

        uint32_t width = ::max(a->getWidth(), b->getWidth());
        uint32_t height = ::max(a->getHeight(), b->getHeight());
        l->texels.resize(width * height);
        textureStructs[l->getId()].width = width;
        textureStructs[l->getId()].height = height;

        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                vec2 off = vec2(1 / float(width), 1 / float(height)); 
                vec2 uv = vec2(x / float(width), y / float(height)) + .5f * off;
                l->texels[y * width + x] = glm::mix(a->sample(uv), b->sample(uv), mix);
            }
        }
        
        l->markDirty();
    };

    try {
        return StaticFactory::create<Texture>(editMutex, name, "Texture", lookupTable, textures, MAX_TEXTURES, create);
    } catch (...) {
		StaticFactory::removeIfExists(editMutex, name, "Texture", lookupTable, textures, MAX_TEXTURES);
		throw;
	}
}

vec4 Texture::sample(vec2 uv) {
    uint32_t width = textureStructs[id].width;
    uint32_t height = textureStructs[id].height;
    vec2 coord = uv * vec2(width-1, height-1);
    ivec2 coord_floor = glm::ivec2(glm::floor(coord));
    ivec2 coord_ceil = glm::ivec2(glm::ceil(coord));
    return texels[coord_floor.y * width + coord_floor.x]; // todo, interpolate four surrouding pixels
    // vec2 remainder = coord - vec2(coord_floor);
    // return glm::mix(
    //     texels[coord_floor.y * width + coord_floor.x],
    //     texels[coord_ceil.y * width + coord_ceil.x],
    //     remainder);
}

std::shared_ptr<std::mutex> Texture::getEditMutex()
{
	return editMutex;
}

Texture* Texture::get(std::string name) {
    return StaticFactory::get(editMutex, name, "Texture", lookupTable, textures, MAX_TEXTURES);
}

void Texture::remove(std::string name) {
    StaticFactory::remove(editMutex, name, "Texture", lookupTable, textures, MAX_TEXTURES);
    anyDirty = true;
}

Texture* Texture::getFront() {
    return textures;
}

TextureStruct* Texture::getFrontStruct() {
    return textureStructs;
}

uint32_t Texture::getCount() {
    return MAX_TEXTURES;
}

std::string Texture::getName()
{
    return name;
}

std::map<std::string, uint32_t> Texture::getNameToIdMap()
{
	return lookupTable;
}