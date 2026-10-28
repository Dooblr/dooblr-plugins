#include "SamplePlayer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dooblrkick::dsp
{
    namespace
    {
        constexpr int zeroCrossings = 16;
        constexpr int resolution = 512;
        constexpr double pi = 3.14159265358979323846;

        struct KernelTable
        {
            std::array<float, (size_t) (zeroCrossings * resolution + 2)> values {};

            KernelTable() noexcept
            {
                for (size_t j = 0; j < values.size(); ++j)
                {
                    const auto x = (double) j / resolution;

                    if (x >= zeroCrossings)
                    {
                        values[j] = 0.0f;
                        continue;
                    }

                    const auto sinc = j == 0 ? 1.0 : std::sin (pi * x) / (pi * x);
                    const auto u = pi * x / zeroCrossings;
                    const auto window = 0.35875 + 0.48829 * std::cos (u) + 0.14128 * std::cos (2.0 * u) + 0.01168 * std::cos (3.0 * u);
                    values[j] = (float) (sinc * window);
                }
            }
        };

        const KernelTable& table() noexcept
        {
            static const KernelTable kernel;
            return kernel;
        }

        inline float kernelAt (const KernelTable& t, float x) noexcept
        {
            const auto scaled = std::abs (x) * (float) resolution;
            const auto index = (int) scaled;

            if (index >= zeroCrossings * resolution)
                return 0.0f;

            const auto frac = scaled - (float) index;
            const auto a = t.values[(size_t) index];
            return a + (t.values[(size_t) index + 1] - a) * frac;
        }
    }

    void SampleReader::prepareTables() noexcept
    {
        (void) table();
    }

    float SampleReader::read (const SampleData* data, double ratio) noexcept
    {
        if (data == nullptr || data->samples.empty())
            return 0.0f;

        const auto length = (int) data->samples.size();

        if (position >= (double) (length + zeroCrossings))
            return 0.0f;

        ratio = std::clamp (ratio, 1.0 / 64.0, 4.0);
        const auto cut = ratio > 1.0 ? (float) (1.0 / ratio) : 1.0f;
        const auto base = (int) std::floor (position);
        const auto frac = (float) (position - (double) base);
        const auto half = (int) std::ceil ((float) zeroCrossings / cut);
        const auto& t = table();
        const auto* x = data->samples.data();

        const auto first = std::max (-half + 1, -base);
        const auto last = std::min (half, length - 1 - base);
        auto sum = 0.0f;

        for (int k = first; k <= last; ++k)
            sum += x[base + k] * kernelAt (t, ((float) k - frac) * cut);

        position += ratio;
        return sum * cut;
    }
}
