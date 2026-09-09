#include <cmath>
#include <cstdlib>
#include <iostream>

#include "hle/rt64_rigid_body.h"
#include "rt64_extended_gbi.h"

namespace {

void expect_finite(float value, const char* context) {
    if (!std::isfinite(value)) {
        std::cerr << "FAIL: " << context << " produced a non-finite angular velocity\n";
        std::exit(1);
    }
}

void expect_near(float actual, float expected, float tolerance, const char* context) {
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << context << " expected " << expected << ", got " << actual << '\n';
        std::exit(1);
    }
}

hlslpp::float4x4 flat_transform(int zero_axis) {
    hlslpp::float4x4 transform = hlslpp::float4x4::identity();
    transform[zero_axis][0] = 0.0f;
    transform[zero_axis][1] = 0.0f;
    transform[zero_axis][2] = 0.0f;
    return transform;
}

} // namespace

int main() {
    // Lamborghini's car-part matrices can be singular (the live matcher reports
    // determinant zero). RT64 normalizes and inverts their 3x3 basis while deriving
    // angular velocity. Before the fix, each of these cases stores NaN, which then
    // reaches the transform candidate score and invalidates stable_sort's ordering.
    for (int zero_axis = 0; zero_axis < 3; zero_axis++) {
        const hlslpp::float4x4 transform = flat_transform(zero_axis);
        RT64::RigidBody body;
        body.updateAngular(transform, transform, G_EX_COMPONENT_AUTO,
            G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO);
        expect_finite(body.angularVelocity, "singular car-part transform");
        if (body.angularVelocityValid) {
            std::cerr << "FAIL: singular car-part transform was marked measurable\n";
            return 1;
        }
    }

    RT64::RigidBody ordinary;
    const hlslpp::float4x4 identity = hlslpp::float4x4::identity();
    ordinary.updateAngular(identity, identity, G_EX_COMPONENT_AUTO,
        G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO);
    expect_finite(ordinary.angularVelocity, "identity transform");
    if (!ordinary.angularVelocityValid) {
        std::cerr << "FAIL: identity transform was marked unmeasurable\n";
        return 1;
    }

    hlslpp::float4x4 quarter_turn = identity;
    quarter_turn[0][0] = 0.0f;
    quarter_turn[0][2] = 1.0f;
    quarter_turn[2][0] = -1.0f;
    quarter_turn[2][2] = 0.0f;
    RT64::RigidBody rotating;
    rotating.updateAngular(identity, quarter_turn, G_EX_COMPONENT_AUTO,
        G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO);
    if (!rotating.angularVelocityValid) {
        std::cerr << "FAIL: ordinary rotation was marked unmeasurable\n";
        return 1;
    }
    expect_near(rotating.angularVelocity, std::acos(0.0f), 1e-5f, "quarter-turn rotation");

    for (float degrees : {0.0f, 5.0f, 19.0f, 21.0f, 90.0f, 180.0f}) {
        const float radians = degrees * std::acos(-1.0f) / 180.0f;
        auto view = identity;
        view[0][0] = view[2][2] = std::cos(radians);
        view[0][2] = std::sin(radians);
        view[2][0] = -std::sin(radians);
        view[3][0] = 100.0f;
        RT64::RigidBody camera;
        camera.updateLinear(identity, view, G_EX_COMPONENT_INTERPOLATE);
        camera.updateAngular(identity, view, G_EX_COMPONENT_INTERPOLATE,
            G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE);
        camera.updateDecomposition(view, false);
        const bool cut = camera.resetOnCameraCut(identity, view);
        if (cut != (degrees > 20.0f)) {
            std::cerr << "FAIL: camera cut classification at " << degrees << " degrees\n";
            return 1;
        }
        if (cut) {
            const auto midpoint = camera.lerp(0.5f, identity, view, true);
            expect_near(hlslpp::determinant(midpoint), 1.0f, 1e-5f,
                "cut must not collapse the camera halfway through");
            expect_near(midpoint[3][0], 100.0f, 1e-5f,
                "translation must cut together with rotation");
            if (camera.lerpRotation || camera.lerpTranslation || camera.lerpScale ||
                camera.lerpSkew || camera.lerpPerspective) {
                std::cerr << "FAIL: cut retained interpolation history\n";
                return 1;
            }
        }
    }

    auto roll_cut = identity;
    roll_cut[0][0] = roll_cut[1][1] = -1.0f;
    RT64::RigidBody roll_camera;
    if (!roll_camera.resetOnCameraCut(identity, roll_cut)) {
        std::cerr << "FAIL: roll-only camera cut was missed\n";
        return 1;
    }
    auto scaled_view = identity;
    scaled_view[0][0] = 2.0f;
    scaled_view[1][1] = 3.0f;
    scaled_view[2][2] = 4.0f;
    RT64::RigidBody scaled_camera;
    if (scaled_camera.resetOnCameraCut(identity, scaled_view)) {
        std::cerr << "FAIL: camera scale change was classified as a rotation cut\n";
        return 1;
    }
    if (!scaled_camera.resetOnCameraCut(identity, flat_transform(0))) {
        std::cerr << "FAIL: invalid camera basis retained interpolation history\n";
        return 1;
    }

    std::cout << "RT64 angular velocity and camera cuts remain safe\n";
    return 0;
}
