#pragma once
/// @file ParticleSystem3D.hpp
/// @brief 3D空間のパーティクルシステム

#include <mitiru/render/Billboard3D.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mitiru::render {

struct Particle3D {
    sgc::Vec3f position{};
    sgc::Vec3f velocity{};
    sgc::Colorf color{1,1,1,1};
    sgc::Colorf endColor{1,1,1,0}; // fade to
    float size = 0.2f;
    float endSize = 0.0f;
    float life = 1.0f;      // remaining life (seconds)
    float maxLife = 1.0f;
    bool alive = false;
};

struct ParticleEmitter3D {
    sgc::Vec3f position{};
    sgc::Vec3f emitDirection{0, 1, 0};
    float emitSpread = 0.5f;      // cone angle (radians)
    float emitSpeed = 2.0f;
    float emitSpeedVariance = 0.5f;
    sgc::Colorf startColor{1,1,1,1};
    sgc::Colorf endColor{1,1,1,0};
    float startSize = 0.3f;
    float endSize = 0.0f;
    float particleLife = 1.0f;
    float particleLifeVariance = 0.3f;
    float emitRate = 10.0f;        // particles per second
    sgc::Vec3f gravity{0, -1.0f, 0};
    bool active = true;
};

class ParticleSystem3D {
public:
    explicit ParticleSystem3D(int maxParticles = 256)
        : m_particles(static_cast<std::size_t>(maxParticles))
    {
    }

    void setEmitter(const ParticleEmitter3D& emitter) { m_emitter = emitter; }
    ParticleEmitter3D& emitter() noexcept { return m_emitter; }
    const ParticleEmitter3D& emitter() const noexcept { return m_emitter; }

    void update(float dt) {
        // Update existing particles
        for (auto& p : m_particles) {
            if (!p.alive) continue;
            p.life -= dt;
            if (p.life <= 0.0f) { p.alive = false; continue; }

            p.velocity += m_emitter.gravity * dt;
            p.position += p.velocity * dt;
        }

        // Emit new particles
        if (m_emitter.active) {
            m_emitAccum += m_emitter.emitRate * dt;
            while (m_emitAccum >= 1.0f) {
                emitOne();
                m_emitAccum -= 1.0f;
            }
        }
    }

    /// @brief パーティクルをビルボードインスタンスに変換する
    void fillBillboards(Billboard3D& billboards) const {
        for (const auto& p : m_particles) {
            if (!p.alive) continue;

            const float t = 1.0f - (p.life / p.maxLife); // 0→1 over lifetime

            BillboardInstance bb;
            bb.position = p.position;
            bb.width = bb.height = lerp(p.size, p.endSize, t);
            bb.color = lerpColor(p.color, p.endColor, t);
            billboards.add(bb);
        }
    }

    [[nodiscard]] int aliveCount() const noexcept {
        int count = 0;
        for (const auto& p : m_particles)
            if (p.alive) ++count;
        return count;
    }

    void clear() {
        for (auto& p : m_particles) p.alive = false;
        m_emitAccum = 0;
    }

private:
    std::vector<Particle3D> m_particles;
    ParticleEmitter3D m_emitter;
    float m_emitAccum = 0;
    uint32_t m_rng = 12345;

    void emitOne() {
        for (auto& p : m_particles) {
            if (p.alive) continue;

            p.alive = true;
            p.position = m_emitter.position;

            // Random direction within emission cone
            float rx = randFloat() * 2.0f - 1.0f;
            float ry = randFloat() * 2.0f - 1.0f;
            float rz = randFloat() * 2.0f - 1.0f;
            sgc::Vec3f randomDir{
                m_emitter.emitDirection.x + rx * m_emitter.emitSpread,
                m_emitter.emitDirection.y + ry * m_emitter.emitSpread,
                m_emitter.emitDirection.z + rz * m_emitter.emitSpread
            };
            float len = std::sqrt(randomDir.x*randomDir.x + randomDir.y*randomDir.y + randomDir.z*randomDir.z);
            if (len > 0.001f) { randomDir.x /= len; randomDir.y /= len; randomDir.z /= len; }

            float speed = m_emitter.emitSpeed + (randFloat() - 0.5f) * 2.0f * m_emitter.emitSpeedVariance;
            p.velocity = randomDir * speed;

            p.color = m_emitter.startColor;
            p.endColor = m_emitter.endColor;
            p.size = m_emitter.startSize;
            p.endSize = m_emitter.endSize;
            p.maxLife = m_emitter.particleLife + (randFloat() - 0.5f) * 2.0f * m_emitter.particleLifeVariance;
            p.life = p.maxLife;
            return;
        }
    }

    float randFloat() {
        m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5;
        return static_cast<float>(m_rng & 0xFFFF) / 65535.0f;
    }

    static float lerp(float a, float b, float t) { return a + (b - a) * t; }
    static sgc::Colorf lerpColor(const sgc::Colorf& a, const sgc::Colorf& b, float t) {
        return {lerp(a.r,b.r,t), lerp(a.g,b.g,t), lerp(a.b,b.b,t), lerp(a.a,b.a,t)};
    }
};

} // namespace mitiru::render
