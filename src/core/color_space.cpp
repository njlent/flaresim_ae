#include "color_space.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>

namespace {

// Linear RGB to linear Rec.709 (D65), white points adapted with Bradford,
// and the inverses. Derived from the published primaries.
constexpr ColorMatrix kIdentity {{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}};

constexpr ColorMatrix kACEScgToRec709 {{1.7050509927f, -0.6217921207f, -0.0832588720f,
                                        -0.1302564175f, 1.1408047366f, -0.0105483191f,
                                        -0.0240033568f, -0.1289689761f, 1.1529723329f}};
constexpr ColorMatrix kRec709ToACEScg {{0.6130974024f, 0.3395231462f, 0.0473794514f,
                                        0.0701937225f, 0.9163538791f, 0.0134523985f,
                                        0.0206155929f, 0.1095697729f, 0.8698146342f}};

constexpr ColorMatrix kACES2065ToRec709 {{2.5216861867f, -1.1341309882f, -0.3875551985f,
                                          -0.2764799142f, 1.3727190877f, -0.0962391734f,
                                          -0.0153780650f, -0.1529753359f, 1.1683534008f}};
constexpr ColorMatrix kRec709ToACES2065 {{0.4396329819f, 0.3829886982f, 0.1773783199f,
                                          0.0897764430f, 0.8134394287f, 0.0967841283f,
                                          0.0175411704f, 0.1115465533f, 0.8709122763f}};

constexpr ColorMatrix kRec2020ToRec709 {{1.6604910021f, -0.5876411388f, -0.0728498633f,
                                         -0.1245504745f, 1.1328998971f, -0.0083494226f,
                                         -0.0181507634f, -0.1005788980f, 1.1187296614f}};
constexpr ColorMatrix kRec709ToRec2020 {{0.6274038959f, 0.3292830384f, 0.0433130657f,
                                         0.0690972894f, 0.9195403951f, 0.0113623156f,
                                         0.0163914389f, 0.0880133079f, 0.8955952532f}};

constexpr ColorMatrix kP3ToRec709 {{1.2249401763f, -0.2249401763f, 0.0f,
                                    -0.0420569547f, 1.0420569547f, 0.0f,
                                    -0.0196375546f, -0.0786360456f, 1.0982736001f}};
constexpr ColorMatrix kRec709ToP3 {{0.8224619687f, 0.1775380313f, 0.0f,
                                    0.0331941989f, 0.9668058011f, 0.0f,
                                    0.0170826307f, 0.0723974407f, 0.9105199286f}};

// RGB to XYZ adapted to D50 (Bradford): what an ICC matrix profile stores
// in its rXYZ, gXYZ and bXYZ tags (one column per tag).
struct IccColorants
{
    WorkingColorSpace space;
    float m[9];
};

constexpr IccColorants kIccColorants[] = {
    {WorkingColorSpace::LinearRec709,
     {0.4360657469f, 0.3851515096f, 0.1430784200f,
      0.2224931771f, 0.7168870131f, 0.0606198098f,
      0.0139239215f, 0.0970813242f, 0.7140993568f}},
    {WorkingColorSpace::ACEScg,
     {0.6899115164f, 0.1497877436f, 0.1245964164f,
      0.2845279995f, 0.6716655209f, 0.0438064796f,
      -0.0060452510f, 0.0099963870f, 0.8211534666f}},
    {WorkingColorSpace::ACES2065_1,
     {0.9909340448f, 0.0122616831f, -0.0389000515f,
      0.3619208961f, 0.7224747440f, -0.0843956401f,
      -0.0027107421f, 0.0082342471f, 0.8195810975f}},
    {WorkingColorSpace::LinearRec2020,
     {0.6735154632f, 0.1656972637f, 0.1250829495f,
      0.2790590051f, 0.6753180057f, 0.0456229891f,
      -0.0019324271f, 0.0299778268f, 0.7970592029f}},
    {WorkingColorSpace::LinearDisplayP3,
     {0.5151464430f, 0.2920099821f, 0.1571392514f,
      0.2412003221f, 0.6922225411f, 0.0665771367f,
      -0.0010501391f, 0.0418782702f, 0.7842764715f}},
};

// Profiles round colorants to s15Fixed16 and some use slightly different
// adaptation; spaces we tell apart differ by far more than this.
constexpr float kIccColorantTolerance = 0.01f;

std::uint32_t read_be32(const unsigned char* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

constexpr std::uint32_t four_cc(const char (&s)[5])
{
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(s[0])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[2])) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(s[3]));
}

bool read_xyz_tag(const unsigned char* bytes, std::size_t size, std::uint32_t offset, std::uint32_t length, float out[3])
{
    if (length < 20 || offset > size || size - offset < 20 || read_be32(bytes + offset) != four_cc("XYZ ")) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        const auto raw = static_cast<std::int32_t>(read_be32(bytes + offset + 8 + 4 * i));
        out[i] = static_cast<float>(raw) / 65536.0f;
    }
    return true;
}

} // namespace

const ColorMatrix& working_to_rec709_matrix(WorkingColorSpace space)
{
    switch (space) {
        case WorkingColorSpace::ACEScg: return kACEScgToRec709;
        case WorkingColorSpace::ACES2065_1: return kACES2065ToRec709;
        case WorkingColorSpace::LinearRec2020: return kRec2020ToRec709;
        case WorkingColorSpace::LinearDisplayP3: return kP3ToRec709;
        case WorkingColorSpace::LinearRec709: break;
    }
    return kIdentity;
}

const ColorMatrix& rec709_to_working_matrix(WorkingColorSpace space)
{
    switch (space) {
        case WorkingColorSpace::ACEScg: return kRec709ToACEScg;
        case WorkingColorSpace::ACES2065_1: return kRec709ToACES2065;
        case WorkingColorSpace::LinearRec2020: return kRec709ToRec2020;
        case WorkingColorSpace::LinearDisplayP3: return kRec709ToP3;
        case WorkingColorSpace::LinearRec709: break;
    }
    return kIdentity;
}

void apply_color_matrix(const ColorMatrix& matrix, MutableRgbImageView image)
{
    if (!image.r || !image.g || !image.b || image.width <= 0 || image.height <= 0) {
        return;
    }
    const std::size_t count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    for (std::size_t i = 0; i < count; ++i) {
        apply_color_matrix(matrix, image.r[i], image.g[i], image.b[i]);
    }
}

bool working_space_from_icc_profile(const void* data, std::size_t size, WorkingColorSpace& out_space)
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (!bytes || size < 132 || read_be32(bytes + 16) != four_cc("RGB ")) {
        return false;
    }
    const std::uint32_t tag_count = read_be32(bytes + 128);
    if (tag_count > (size - 132) / 12) {
        return false;
    }

    float columns[3][3] {};
    bool found[3] {};
    constexpr std::uint32_t kTags[3] = {four_cc("rXYZ"), four_cc("gXYZ"), four_cc("bXYZ")};
    for (std::uint32_t t = 0; t < tag_count; ++t) {
        const unsigned char* entry = bytes + 132 + 12 * t;
        const std::uint32_t signature = read_be32(entry);
        for (int c = 0; c < 3; ++c) {
            if (signature == kTags[c]) {
                found[c] = read_xyz_tag(bytes, size, read_be32(entry + 4), read_be32(entry + 8), columns[c]);
            }
        }
    }
    if (!found[0] || !found[1] || !found[2]) {
        return false;
    }

    for (const IccColorants& candidate : kIccColorants) {
        float worst = 0.0f;
        for (int row = 0; row < 3; ++row) {
            for (int c = 0; c < 3; ++c) {
                worst = std::max(worst, std::abs(candidate.m[row * 3 + c] - columns[c][row]));
            }
        }
        if (worst < kIccColorantTolerance) {
            out_space = candidate.space;
            return true;
        }
    }
    return false;
}

bool working_space_from_name(std::string_view name, WorkingColorSpace& out_space)
{
    std::string lower;
    lower.reserve(name.size());
    for (const char c : name) {
        // Drop separators so "ACES - ACEScg", "lin_ap1" and "Rec. 2020" match alike.
        if (std::isalnum(static_cast<unsigned char>(c))) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    auto has = [&](const char* token) { return lower.find(token) != std::string::npos; };

    if (has("acescct") || (has("acescc") && !has("acescg"))) {
        return false;  // log encodings, not a linear working space
    }
    if (has("acescg") || has("ap1")) {
        out_space = WorkingColorSpace::ACEScg;
    } else if (has("aces2065") || has("ap0")) {
        out_space = WorkingColorSpace::ACES2065_1;
    } else if (has("2020")) {
        out_space = WorkingColorSpace::LinearRec2020;
    } else if (has("p3")) {
        out_space = WorkingColorSpace::LinearDisplayP3;
    } else if (has("709") || has("srgb")) {
        out_space = WorkingColorSpace::LinearRec709;
    } else {
        return false;
    }
    return true;
}

const char* working_space_label(WorkingColorSpace space)
{
    switch (space) {
        case WorkingColorSpace::ACEScg: return "ACEScg";
        case WorkingColorSpace::ACES2065_1: return "ACES2065-1";
        case WorkingColorSpace::LinearRec2020: return "Linear Rec.2020";
        case WorkingColorSpace::LinearDisplayP3: return "Linear Display P3";
        case WorkingColorSpace::LinearRec709: break;
    }
    return "Linear Rec.709 / sRGB";
}
