// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "EditStageMetrics.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Utils.h>

// USD
#include <pxr/base/gf/rotation.h>
#include <pxr/base/tf/patternMatcher.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/curves.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/plane.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdPhysics/limitAPI.h>
#include <pxr/usd/usdPhysics/massAPI.h>
#include <pxr/usd/usdSkel/animation.h>


PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::EditStageMetricsOperation);


namespace omni::scene::optimizer
{

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((xformOpTransform, "xformOp:transform"))
    ((xformOpRotateUpAxisCorrection, "xformOp:rotateX:upAxisCorrection"))
    (Mesh)
    ((apiSchemas, "apiSchemas"))
    ((PhysicsLimitAPITransY, "PhysicsLimitAPI:transY"))
    ((PhysicsLimitAPITransZ, "PhysicsLimitAPI:transZ"))
    ((PhysicsLimitAPIRotY, "PhysicsLimitAPI:rotY"))
    ((PhysicsLimitAPIRotZ, "PhysicsLimitAPI:rotZ"))
    ((LimitIdentifierTransX, "transX"))
    ((LimitIdentifierTransY, "transY"))
    ((LimitIdentifierTransZ, "transZ"))
    ((LimitIdentifierRotY, "rotY"))
    ((LimitIdentifierRotZ, "rotZ"))
    ((LimitTransYHigh, "limit:transY:physics:high"))
    ((LimitTransYLow, "limit:transY:physics:low"))
    ((LimitTransZHigh, "limit:transZ:physics:high"))
    ((LimitTransZLow, "limit:transZ:physics:low"))
    ((LimitRotYHigh, "limit:rotY:physics:high"))
    ((LimitRotYLow, "limit:rotY:physics:low"))
    ((LimitRotZHigh, "limit:rotZ:physics:high"))
    ((LimitRotZLow, "limit:rotZ:physics:low"))
);
// LCOV_EXCL_STOP
// clang-format on


// Constants
constexpr const char* s_category = "EDIT_STAGE_METRICS";

// constants representing the 3 axis
static const GfVec3d X_AXIS(1.0, 0.0, 0.0);
static const GfVec3d Y_AXIS(0.0, 1.0, 0.0);
static const GfVec3d Z_AXIS(0.0, 0.0, 1.0);
static const std::vector<GfVec3d> AXIS = { X_AXIS, Y_AXIS, Z_AXIS };


/// Scales a SdfAttributeSpec with a single value type (e.g. float or GfVec3f) by a given scale factor.
template <typename T>
static void _scaleSingleType(const SdfAttributeSpecHandle& attrSpec, const double scale)
{
    // scale the default value if there is one
    if (attrSpec->HasDefaultValue())
    {
        T value = attrSpec->GetDefaultValue().UncheckedGet<T>();
        value *= scale;
        attrSpec->SetDefaultValue(VtValue(value));
    }

    // scale the time samples if the attribute has them
    SdfTimeSampleMap timeSamples = attrSpec->GetTimeSampleMap();
    if (!timeSamples.empty())
    {
        for (auto& timeSample : timeSamples)
        {
            T value = timeSample.second.UncheckedGet<T>();
            value *= scale;
            timeSample.second = VtValue(value);
        }
        attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
    }
}


/// Scales a SdfAttributeSpec with an array value type (e.g. VtFloatArray or VtVec3fArray) by a given scale factor.
/// Where T is the datatype of the array elements.
template <typename T>
static void _scaleArrayType(const SdfAttributeSpecHandle& attrSpec, const double scale)
{
    // scale the default value if there is one
    if (attrSpec->HasDefaultValue())
    {
        VtArray<T> values = attrSpec->GetDefaultValue().UncheckedGet<VtArray<T>>();
        for (T& v : values)
        {
            v *= scale;
        }
        attrSpec->SetDefaultValue(VtValue(values));
    }

    // scale the time samples if the attribute has them
    SdfTimeSampleMap timeSamples = attrSpec->GetTimeSampleMap();
    if (!timeSamples.empty())
    {
        for (auto& timeSample : timeSamples)
        {
            VtArray<T> values = timeSample.second.UncheckedGet<VtArray<T>>();
            for (T& v : values)
            {
                v *= scale;
            }
            timeSample.second = VtValue(values);
        }
        attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
    }
}


/// Scales the translation component of a GfMatrix4d by a given scale factor.
static void _scaleMatrix4d(GfMatrix4d& matrix, const double scale)
{
    GfVec3d translate = matrix.ExtractTranslation();
    translate *= scale;
    matrix.SetTranslateOnly(translate);
}


/// Rotates a SdfAttributeSpec with a single value type (e.g. GfVec3f) by a given GfRotation.
template <typename T>
static void _rotateSingleType(const SdfAttributeSpecHandle& attrSpec, const GfRotation& rotation)
{
    // rotate the default value if there is one
    if (attrSpec->HasDefaultValue())
    {
        attrSpec->SetDefaultValue(VtValue(rotation.TransformDir(attrSpec->GetDefaultValue().UncheckedGet<T>())));
    }

    // rotate the time samples if the attribute has them
    SdfTimeSampleMap timeSamples = attrSpec->GetTimeSampleMap();
    if (!timeSamples.empty())
    {
        for (auto& timeSample : timeSamples)
        {
            timeSample.second = VtValue(rotation.TransformDir(timeSample.second.UncheckedGet<T>()));
        }
        attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
    }
}


/// Rotates a SdfAttributeSpec with an array value type (e.g. VtVec3fArray) by a given GfRotation.
/// Where T is the datatype of the array elements.
template <typename T>
static void _rotateArrayType(const SdfAttributeSpecHandle& attrSpec, const GfRotation& rotation)
{
    // rotate the default value if there is one
    if (attrSpec->HasDefaultValue())
    {
        VtArray<T> values = attrSpec->GetDefaultValue().UncheckedGet<VtArray<T>>();
        for (T& v : values)
        {
            v = GfVec3f(rotation.TransformDir(v));
        }
        attrSpec->SetDefaultValue(VtValue(values));
    }

    // rotate the time samples if the attribute has them
    SdfTimeSampleMap timeSamples = attrSpec->GetTimeSampleMap();
    if (!timeSamples.empty())
    {
        for (auto& timeSample : timeSamples)
        {
            VtArray<T> values = timeSample.second.UncheckedGet<VtArray<T>>();
            for (T& v : values)
            {
                v = GfVec3f(rotation.TransformDir(v));
            }
            timeSample.second = VtValue(values);
        }
        attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
    }
}


// Changes the basis of a vec3d representing XYZ euler angles with order based on the axisOrder vector
template <typename T>
static void _changeBasisOfEulerAngles(T& eulerAngles,
                                      const std::vector<size_t>& axisOrder,
                                      const GfMatrix3d& changeOfBasis,
                                      const GfMatrix3d& changeOfBasisInv)
{
    // construct the rotation based on the axis order
    GfRotation rot = GfRotation(AXIS[axisOrder[0]], eulerAngles[axisOrder[0]]) *
                     GfRotation(AXIS[axisOrder[1]], eulerAngles[axisOrder[1]]) *
                     GfRotation(AXIS[axisOrder[2]], eulerAngles[axisOrder[2]]);

    // apply the change of basis to the rotation
    GfMatrix3d matrix = changeOfBasis * GfMatrix3d(rot) * changeOfBasisInv;

    // decompose back into euler angles based on reverse axis order
    const GfVec3d decomposed =
        matrix.ExtractRotation().Decompose(AXIS[axisOrder[2]], AXIS[axisOrder[1]], AXIS[axisOrder[0]]);
    eulerAngles[axisOrder[0]] = static_cast<typename T::ScalarType>(decomposed[2]);
    eulerAngles[axisOrder[1]] = static_cast<typename T::ScalarType>(decomposed[1]);
    eulerAngles[axisOrder[2]] = static_cast<typename T::ScalarType>(decomposed[0]);
}


// Changes the basis of the a vec3 rotate xformOp attribute
template <typename T>
static void _changeBasisOfRotateXformOp(const SdfAttributeSpecHandle& attrSpec,
                                        const std::vector<size_t>& axisOrder,
                                        const GfMatrix3d& changeOfBasis,
                                        const GfMatrix3d& changeOfBasisInv)
{
    // handle default value
    if (attrSpec->HasDefaultValue())
    {
        // get the euler values and change their basis
        T eulerValue = attrSpec->GetDefaultValue().UncheckedGet<T>();
        _changeBasisOfEulerAngles<T>(eulerValue, axisOrder, changeOfBasis, changeOfBasisInv);

        attrSpec->SetDefaultValue(VtValue(eulerValue));
    }

    // handle time samples
    SdfTimeSampleMap timeSamples = attrSpec->GetTimeSampleMap();
    if (!timeSamples.empty())
    {
        for (auto& timeSample : timeSamples)
        {
            // get the euler values and change their basis
            T eulerValue = timeSample.second.UncheckedGet<T>();
            _changeBasisOfEulerAngles<T>(eulerValue, axisOrder, changeOfBasis, changeOfBasisInv);
            timeSample.second = VtValue(eulerValue);
        }
        attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
    }
}

// Changes the basis of the a vec3 scale xformOp attribute - this is a special case since scale can't be rotated as a
// vector
template <typename T>
static void _changeBasisOfScaleXformOp(const SdfAttributeSpecHandle& attrSpec,
                                       const GfMatrix4d& changeOfBasis,
                                       const GfMatrix4d& changeOfBasisInv)
{
    auto scaleVec3Lambda = [&changeOfBasis, &changeOfBasisInv](T& scaleValue)
    {
        static GfMatrix4d r;
        static GfVec3d s;
        static GfMatrix4d u;
        static GfVec3d t;
        static GfMatrix4d p;

        // uniform scale? no work required
        if (scaleValue[0] == scaleValue[1] && scaleValue[1] == scaleValue[2])
        {
            return;
        }

        GfMatrix4d matrix(1.0);
        matrix.SetScale(scaleValue);
        matrix = changeOfBasis * matrix * changeOfBasisInv;
        matrix.Factor(&r, &s, &u, &t, &p);
        scaleValue[0] = static_cast<typename T::ScalarType>(s[0]);
        scaleValue[1] = static_cast<typename T::ScalarType>(s[1]);
        scaleValue[2] = static_cast<typename T::ScalarType>(s[2]);
    };

    // change basis of the default value if there is one
    if (attrSpec->HasDefaultValue())
    {
        T scaleValue = attrSpec->GetDefaultValue().UncheckedGet<T>();
        scaleVec3Lambda(scaleValue);
        attrSpec->SetDefaultValue(VtValue(scaleValue));
    }

    // change basis of time samples if the attribute has them
    SdfTimeSampleMap timeSamples = attrSpec->GetTimeSampleMap();
    if (!timeSamples.empty())
    {
        for (auto& timeSample : timeSamples)
        {
            T scaleValue = timeSample.second.UncheckedGet<T>();
            scaleVec3Lambda(scaleValue);
            timeSample.second = VtValue(scaleValue);
        }
        attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
    }
}


// Rotates the values of a bounding box represented by a VtVec3fArray by a given GfRotation.
// This function ensures that the values of the bounding box are still ordered min -> max after rotation.
static bool _rotateBoundingBox(VtVec3fArray& values, const GfRotation& rotation)
{
    if (values.size() != 2)
    {
        return false;
    }
    GfVec3f min(rotation.TransformDir(values[0]));
    GfVec3f max(rotation.TransformDir(values[1]));
    values[0] = GfVec3f(std::min(min[0], max[0]), std::min(min[1], max[1]), std::min(min[2], max[2]));
    values[1] = GfVec3f(std::max(min[0], max[0]), std::max(min[1], max[1]), std::max(min[2], max[2]));
    return true;
}


// Performs a change of basis on a quaternion attribute with an single quaternion type T
template <typename T>
static void _changeBasisOfSingleQuaternion(const SdfAttributeSpecHandle& attrSpec,
                                           const GfMatrix3d& changeOfBasis,
                                           const GfMatrix3d& changeOfBasisInv)
{
    // change basis of the default value if there is one
    if (attrSpec->HasDefaultValue())
    {
        GfMatrix3d matrix =
            changeOfBasis * GfMatrix3d(GfRotation(attrSpec->GetDefaultValue().UncheckedGet<T>())) * changeOfBasisInv;
        attrSpec->SetDefaultValue(VtValue(T(matrix.ExtractRotation().GetQuat())));
    }

    // change basis of time samples if the attribute has them
    SdfTimeSampleMap timeSamples = attrSpec->GetTimeSampleMap();
    if (!timeSamples.empty())
    {
        for (auto& timeSample : timeSamples)
        {
            GfMatrix3d matrix =
                changeOfBasis * GfMatrix3d(GfRotation(timeSample.second.UncheckedGet<T>())) * changeOfBasisInv;
            timeSample.second = VtValue(T(matrix.ExtractRotation().GetQuat()));
        }
        attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
    }
}


// Performs a change of basis on a quaternion attribute with an array of quaternion type T
template <typename T>
static void _changeBasisOfArrayQuaternion(const SdfAttributeSpecHandle& attrSpec,
                                          const GfMatrix3d& changeOfBasis,
                                          const GfMatrix3d& changeOfBasisInv)
{
    // change basis of the default value if there is one
    if (attrSpec->HasDefaultValue())
    {
        VtArray<T> quats = attrSpec->GetDefaultValue().UncheckedGet<VtArray<T>>();
        for (T& quat : quats)
        {
            GfMatrix3d matrix = changeOfBasis * GfMatrix3d(GfRotation(quat)) * changeOfBasisInv;
            quat = T(matrix.ExtractRotation().GetQuat());
        }
        attrSpec->SetDefaultValue(VtValue(quats));
    }

    // change basis of time samples if the attribute has them
    SdfTimeSampleMap timeSamples = attrSpec->GetTimeSampleMap();
    if (!timeSamples.empty())
    {
        for (auto& timeSample : timeSamples)
        {
            VtArray<T> quats = timeSample.second.UncheckedGet<VtArray<T>>();
            for (T& quat : quats)
            {
                GfMatrix3d matrix = changeOfBasis * GfMatrix3d(GfRotation(quat)) * changeOfBasisInv;
                quat = T(matrix.ExtractRotation().GetQuat());
            }
            timeSample.second = VtValue(quats);
        }
        attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
    }
}


// performs a change of basis on a matrix attribute with a single matrix type T
template <typename T>
static void _changeBasisOfSingleMatrix(const SdfAttributeSpecHandle& attrSpec,
                                       const T& changeOfBasis,
                                       const T& changeOfBasisInv)
{
    // change basis of the default value if there is one
    if (attrSpec->HasDefaultValue())
    {
        attrSpec->SetDefaultValue(
            VtValue(changeOfBasis * attrSpec->GetDefaultValue().UncheckedGet<T>() * changeOfBasisInv));
    }

    // change basis of time samples if the attribute has them
    SdfTimeSampleMap timeSamples = attrSpec->GetTimeSampleMap();
    if (!timeSamples.empty())
    {
        for (auto& timeSample : timeSamples)
        {
            timeSample.second = VtValue(changeOfBasis * timeSample.second.UncheckedGet<T>() * changeOfBasisInv);
        }
        attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
    }
}


// performs a change of basis on a matrix attribute with an array of matrix type Ts
template <typename T>
static void _changeBasisOfArrayMatrix(const SdfAttributeSpecHandle& attrSpec,
                                      const T& changeOfBasis,
                                      const T& changeOfBasisInv)
{
    // change basis of the default value if there is one
    if (attrSpec->HasDefaultValue())
    {
        VtArray<T> matrices = attrSpec->GetDefaultValue().UncheckedGet<VtArray<T>>();
        for (T& matrix : matrices)
        {
            matrix = changeOfBasis * matrix * changeOfBasisInv;
        }
        attrSpec->SetDefaultValue(VtValue(matrices));
    }

    // change basis of time samples if the attribute has them
    SdfTimeSampleMap timeSamples = attrSpec->GetTimeSampleMap();
    if (!timeSamples.empty())
    {
        for (auto& timeSample : timeSamples)
        {
            VtArray<T> matrices = timeSample.second.UncheckedGet<VtArray<T>>();
            for (T& matrix : matrices)
            {
                matrix = changeOfBasis * matrix * changeOfBasisInv;
            }
            timeSample.second = VtValue(matrices);
        }
        attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
    }
}


/// Returns whether this AttributeSpec matches convention for an xformOp attribute
static bool _isXformOpAttribute(const SdfAttributeSpecHandle& attrSpec)
{
    static const TfPatternMatcher xformPattern("xformOp:");
    return xformPattern.Match(attrSpec->GetName());
}


/// Returns whether this AttributeSpec matches convention for a translate attribute.
static bool _isTranslateAttribute(const SdfAttributeSpecHandle& attrSpec)
{
    static const TfPatternMatcher translatePattern("xformOp:translate(:.*)?");
    return translatePattern.Match(attrSpec->GetName());
}


/// Returns whether this AttributeSpec matches convention for a rotate attribute.
static bool _isRotateAttribute(const SdfAttributeSpecHandle& attrSpec)
{
    static const TfPatternMatcher pattern("xformOp:rotate(.*)?");
    return pattern.Match(attrSpec->GetName());
}


/// Returns whether this AttributeSpec matches convention for a scale attribute.
static bool _isScaleAttribute(const SdfAttributeSpecHandle& attrSpec)
{
    static const TfPatternMatcher pattern("xformOp:scale(:.*)?");
    return pattern.Match(attrSpec->GetName());
}


/// Returns whether this AttributeSpec matches convention for a extent attribute.
static bool _isExtentAttribute(const SdfAttributeSpecHandle& attrSpec)
{
    return (attrSpec->GetName() == "extent" || attrSpec->GetName() == "extentsHint") &&
           attrSpec->GetTypeName().GetType().IsA<VtVec3fArray>();
}


/// Hash functor so we can use SdfPaths in a hash set
struct SdfPathHashFunctor
{
    size_t operator()(const SdfPath& path) const
    {
        return path.GetHash();
    }
};


/// Object to group all pre-computed change of basis related matrices and rotation together
class ChangeOfBasisPrecompute
{
public:
    GfMatrix4d m_changeOfBasis;
    GfMatrix4d m_changeOfBasisInv;
    GfMatrix3d m_changeOfBasis3;
    GfMatrix3d m_changeOfBasis3Inv;
    GfRotation m_rotation;

    ChangeOfBasisPrecompute()
        : m_changeOfBasis(1.0)
        , m_changeOfBasisInv(1.0)
        , m_changeOfBasis3(1.0)
        , m_changeOfBasis3Inv(1.0)
    {
    }

    void YtoZ()
    {
        // clang-format off
        m_changeOfBasis = GfMatrix4d(
            1.0, 0.0,  0.0, 0.0,
            0.0, 0.0, -1.0, 0.0,
            0.0, 1.0,  0.0, 0.0,
            0.0, 0.0,  0.0, 1.0
        );
        // clang-format on
        m_rotation.SetAxisAngle(GfVec3d(1.0, 0.0, 0.0), 90.0);

        precompute();
    }

    void ZtoY()
    {
        // clang-format off
        m_changeOfBasis = GfMatrix4d(
            1.0,  0.0, 0.0, 0.0,
            0.0,  0.0, 1.0, 0.0,
            0.0, -1.0, 0.0, 0.0,
            0.0,  0.0, 0.0, 1.0
        );
        // clang-format on
        m_rotation.SetAxisAngle(GfVec3d(1.0, 0.0, 0.0), -90.0);

        precompute();
    }

private:
    void precompute()
    {
        m_changeOfBasisInv = m_changeOfBasis.GetInverse();
        m_changeOfBasis3.SetRow(0, m_changeOfBasis.GetRow3(0));
        m_changeOfBasis3.SetRow(1, m_changeOfBasis.GetRow3(1));
        m_changeOfBasis3.SetRow(2, m_changeOfBasis.GetRow3(2));
        m_changeOfBasis3Inv = m_changeOfBasis3.GetInverse();
    }
};


/// Base helper object which handles performing the actual transformation of scaling and/or changing the basis of an
/// attribute
class AttributeTransformer
{
public:
    AttributeTransformer(const SdfPath& path)
        : m_path(path)
        , m_doScale(false)
        , m_doChangeOfBasis(false)
    {
    }

    virtual ~AttributeTransformer() = default;

    /// Sets this Transformer to perform scaling
    void setScale(double scale)
    {
        m_doScale = true;
        m_scale = scale;
    }

    /// Sets this Transformer to perform change of basis
    /// note: this currently requires a change of basis matrix, and a rotation representing the direction of the basis
    ///       change (maybe we can internally compute the rotation from the matrix?)
    void setChangeOfBasis(const ChangeOfBasisPrecompute& changeOfBasis)
    {
        m_doChangeOfBasis = true;
        m_CoB = changeOfBasis;
    }

    /// Performs the transformation on this object's attribute in the given layer
    virtual void transform(const SdfLayerHandle& layer, std::vector<std::string>& warnings)
    {
        SdfAttributeSpecHandle attrSpec = _getAttributeSpec(layer);
        if (!attrSpec)
        {
            // no attribute spec - no work to do
            return;
        }

        const TfType attrType = attrSpec->GetTypeName().GetType();

        if (m_doScale)
        {
            // handle scaling by type
            if (attrType.IsA<GfHalf>())
            {
                _scaleSingleType<GfHalf>(attrSpec, m_scale);
            }
            else if (attrType.IsA<float>())
            {
                _scaleSingleType<float>(attrSpec, m_scale);
            }
            else if (attrType.IsA<double>())
            {
                _scaleSingleType<double>(attrSpec, m_scale);
            }
            else if (attrType.IsA<GfVec2h>())
            {
                _scaleSingleType<GfVec2h>(attrSpec, m_scale);
            }
            else if (attrType.IsA<GfVec2f>())
            {
                _scaleSingleType<GfVec2f>(attrSpec, m_scale);
            }
            else if (attrType.IsA<GfVec2d>())
            {
                _scaleSingleType<GfVec2d>(attrSpec, m_scale);
            }
            else if (attrType.IsA<GfVec3h>())
            {
                _scaleSingleType<GfVec3h>(attrSpec, m_scale);
            }
            else if (attrType.IsA<GfVec3f>())
            {
                _scaleSingleType<GfVec3f>(attrSpec, m_scale);
            }
            else if (attrType.IsA<GfVec3d>())
            {
                _scaleSingleType<GfVec3d>(attrSpec, m_scale);
            }
            else if (attrType.IsA<VtHalfArray>())
            {
                _scaleArrayType<GfHalf>(attrSpec, m_scale);
            }
            else if (attrType.IsA<VtFloatArray>())
            {
                _scaleArrayType<float>(attrSpec, m_scale);
            }
            else if (attrType.IsA<VtDoubleArray>())
            {
                _scaleArrayType<double>(attrSpec, m_scale);
            }
            else if (attrType.IsA<VtVec3hArray>())
            {
                _scaleArrayType<GfVec3h>(attrSpec, m_scale);
            }
            else if (attrType.IsA<VtVec3fArray>())
            {
                _scaleArrayType<GfVec3f>(attrSpec, m_scale);
            }
            else if (attrType.IsA<VtVec3dArray>())
            {
                _scaleArrayType<GfVec3d>(attrSpec, m_scale);
            }
            // note: GfMatrix4d is handled as a special case, since we need to extract the translation component and
            //       only scale that. For what ever reason UsdAttribute.Get() isn't implemented for GfMatrix4f so we
            //       don't support float matrices at this point
            else if (attrType.IsA<GfMatrix4d>())
            {
                // scale the default value if there is one
                if (attrSpec->HasDefaultValue())
                {
                    GfMatrix4d matrix = attrSpec->GetDefaultValue().Get<GfMatrix4d>();
                    GfVec3d translate = matrix.ExtractTranslation();
                    translate *= m_scale;
                    matrix.SetTranslateOnly(translate);
                    attrSpec->SetDefaultValue(VtValue(matrix));
                }

                // scale the time samples if the attribute has them
                SdfTimeSampleMap timeSamples = attrSpec->GetTimeSampleMap();
                if (!timeSamples.empty())
                {
                    for (auto& timeSample : timeSamples)
                    {
                        GfMatrix4d matrix = timeSample.second.Get<GfMatrix4d>();
                        GfVec3d translate = matrix.ExtractTranslation();
                        translate *= m_scale;
                        matrix.SetTranslateOnly(translate);
                        timeSample.second = VtValue(matrix);
                    }
                    attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
                }
            }
            // GfMatrix4dArray also a special case
            else if (attrType.IsA<VtArray<GfMatrix4d>>())
            {
                // scale the default value if there is one
                if (attrSpec->HasDefaultValue())
                {
                    VtArray<GfMatrix4d> matrices = attrSpec->GetDefaultValue().Get<VtArray<GfMatrix4d>>();
                    for (GfMatrix4d& matrix : matrices)
                    {
                        _scaleMatrix4d(matrix, m_scale);
                    }
                    attrSpec->SetDefaultValue(VtValue(matrices));
                }

                // scale the time samples if the attribute has them
                SdfTimeSampleMap timeSamples = attrSpec->GetTimeSampleMap();
                if (!timeSamples.empty())
                {
                    for (auto& timeSample : timeSamples)
                    {
                        VtArray<GfMatrix4d> matrices = timeSample.second.Get<VtArray<GfMatrix4d>>();
                        for (GfMatrix4d& matrix : matrices)
                        {
                            _scaleMatrix4d(matrix, m_scale);
                        }
                        timeSample.second = VtValue(matrices);
                    }
                    attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
                }
            }
            else
            {
                warnings.push_back("Unsupported attribute type for scaling: " +
                                   attrSpec->GetTypeName().GetAsToken().GetString());
            }
        }

        if (m_doChangeOfBasis)
        {
            // special case for xformOp rotate attributes since they can't be transformed as a vector
            if (_isRotateAttribute(attrSpec))
            {
                // extract the axis order string from the attribute name
                std::string axisOrderStr = attrSpec->GetName().substr(14);
                auto findDelim = axisOrderStr.find_first_of(':');
                if (findDelim != std::string::npos)
                {
                    axisOrderStr = axisOrderStr.substr(0, findDelim);
                }

                // 3 characters in the rotation attribute name? (XYZ, XZY, ZYX, etc)
                if (axisOrderStr.size() == 3)
                {
                    // resolve the ordering of the characters in the string to the order of the axis indices
                    std::vector<size_t> axisOrder;
                    axisOrder.reserve(3);
                    for (char c : axisOrderStr)
                    {
                        if (c == 'X')
                        {
                            axisOrder.push_back(0);
                        }
                        else if (c == 'Y')
                        {
                            axisOrder.push_back(1);
                        }
                        else if (c == 'Z')
                        {
                            axisOrder.push_back(2);
                        }
                    }

                    // handle based on type
                    if (attrType.IsA<GfVec3h>())
                    {
                        _changeBasisOfRotateXformOp<GfVec3h>(attrSpec,
                                                             axisOrder,
                                                             m_CoB.m_changeOfBasis3,
                                                             m_CoB.m_changeOfBasis3Inv);
                    }
                    else if (attrType.IsA<GfVec3f>())
                    {
                        _changeBasisOfRotateXformOp<GfVec3f>(attrSpec,
                                                             axisOrder,
                                                             m_CoB.m_changeOfBasis3,
                                                             m_CoB.m_changeOfBasis3Inv);
                    }
                    else if (attrType.IsA<GfVec3d>())
                    {
                        _changeBasisOfRotateXformOp<GfVec3d>(attrSpec,
                                                             axisOrder,
                                                             m_CoB.m_changeOfBasis3,
                                                             m_CoB.m_changeOfBasis3Inv);
                    }
                    else
                    {
                        warnings.push_back("Unsupported rotate xformOp attribute type for change of basis: " +
                                           attrSpec->GetTypeName().GetAsToken().GetString());
                    }
                }
                // note: 2 axis (e.g. YZ, XZ, etc) are not supported by USD and single character axis (e.g. X, Y, Z)
                //       are special case handled by SingleAxisRotateTransformer since they're complicated
                else
                {
                    warnings.push_back("Unsupported axis order for rotation attribute: " + attrSpec->GetName());
                }
            }
            // special case for xformOp scale attributes since scale can't be transformed as a vector - it just needs to
            // have its components shuffled
            else if (_isScaleAttribute(attrSpec))
            {
                // handle based on type
                if (attrType.IsA<GfVec3h>())
                {
                    _changeBasisOfScaleXformOp<GfVec3h>(attrSpec, m_CoB.m_changeOfBasis, m_CoB.m_changeOfBasisInv);
                }
                else if (attrType.IsA<GfVec3f>())
                {
                    _changeBasisOfScaleXformOp<GfVec3f>(attrSpec, m_CoB.m_changeOfBasis, m_CoB.m_changeOfBasisInv);
                }
                else if (attrType.IsA<GfVec3d>())
                {
                    _changeBasisOfScaleXformOp<GfVec3d>(attrSpec, m_CoB.m_changeOfBasis, m_CoB.m_changeOfBasisInv);
                }
                else
                {
                    warnings.push_back("Unsupported scale xformOp attribute type for change of basis: " +
                                       attrSpec->GetTypeName().GetAsToken().GetString());
                }
            }
            // special case for handling for extent attributes since we need to ensure that it still represents a
            // min -> max bounding box after rotation
            else if (_isExtentAttribute(attrSpec))
            {
                // rotate and conform default value if there is one
                if (attrSpec->HasDefaultValue())
                {
                    VtVec3fArray values = attrSpec->GetDefaultValue().Get<VtVec3fArray>();
                    if (_rotateBoundingBox(values, m_CoB.m_rotation))
                    {
                        attrSpec->SetDefaultValue(VtValue(values));
                    }
                }

                // conform the time samples if the attribute has them
                SdfTimeSampleMap timeSamples = attrSpec->GetTimeSampleMap();
                if (!timeSamples.empty())
                {
                    for (auto& timeSample : timeSamples)
                    {
                        VtVec3fArray values = timeSample.second.Get<VtVec3fArray>();
                        if (_rotateBoundingBox(values, m_CoB.m_rotation))
                        {
                            timeSample.second = VtValue(values);
                        }
                    }
                    attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
                }
            }
            // handle change of basis by type
            else
            {
                if (attrType.IsA<GfVec3f>())
                {
                    _rotateSingleType<GfVec3f>(attrSpec, m_CoB.m_rotation);
                }
                else if (attrType.IsA<GfVec3d>())
                {
                    _rotateSingleType<GfVec3d>(attrSpec, m_CoB.m_rotation);
                }
                else if (attrType.IsA<VtVec3fArray>())
                {
                    _rotateArrayType<GfVec3f>(attrSpec, m_CoB.m_rotation);
                }
                else if (attrType.IsA<VtVec3dArray>())
                {
                    _rotateArrayType<GfVec3d>(attrSpec, m_CoB.m_rotation);
                }
                else if (attrType.IsA<GfQuath>())
                {
                    _changeBasisOfSingleQuaternion<GfQuath>(attrSpec, m_CoB.m_changeOfBasis3, m_CoB.m_changeOfBasis3Inv);
                }
                else if (attrType.IsA<VtArray<GfQuath>>())
                {
                    _changeBasisOfArrayQuaternion<GfQuath>(attrSpec, m_CoB.m_changeOfBasis3, m_CoB.m_changeOfBasis3Inv);
                }
                else if (attrType.IsA<GfQuatf>())
                {
                    _changeBasisOfSingleQuaternion<GfQuatf>(attrSpec, m_CoB.m_changeOfBasis3, m_CoB.m_changeOfBasis3Inv);
                }
                else if (attrType.IsA<VtArray<GfQuatf>>())
                {
                    _changeBasisOfArrayQuaternion<GfQuatf>(attrSpec, m_CoB.m_changeOfBasis3, m_CoB.m_changeOfBasis3Inv);
                }
                else if (attrType.IsA<GfQuatd>())
                {
                    _changeBasisOfSingleQuaternion<GfQuatd>(attrSpec, m_CoB.m_changeOfBasis3, m_CoB.m_changeOfBasis3Inv);
                }
                else if (attrType.IsA<VtArray<GfQuatd>>())
                {
                    _changeBasisOfArrayQuaternion<GfQuatd>(attrSpec, m_CoB.m_changeOfBasis3, m_CoB.m_changeOfBasis3Inv);
                }
                else if (attrType.IsA<GfMatrix3d>())
                {
                    _changeBasisOfSingleMatrix(attrSpec, m_CoB.m_changeOfBasis3, m_CoB.m_changeOfBasis3Inv);
                }
                else if (attrType.IsA<VtArray<GfMatrix3d>>())
                {
                    _changeBasisOfArrayMatrix(attrSpec, m_CoB.m_changeOfBasis3, m_CoB.m_changeOfBasis3Inv);
                }
                else if (attrType.IsA<GfMatrix4d>())
                {
                    _changeBasisOfSingleMatrix(attrSpec, m_CoB.m_changeOfBasis, m_CoB.m_changeOfBasisInv);
                }
                else if (attrType.IsA<VtArray<GfMatrix4d>>())
                {
                    _changeBasisOfArrayMatrix(attrSpec, m_CoB.m_changeOfBasis, m_CoB.m_changeOfBasisInv);
                }
                else
                {
                    warnings.push_back("Unsupported attribute type for change of basis: " +
                                       attrSpec->GetTypeName().GetAsToken().GetString());
                }
            }
        }

        return;
    }

protected:
    SdfPath m_path;
    bool m_doScale;
    double m_scale;
    bool m_doChangeOfBasis;
    ChangeOfBasisPrecompute m_CoB;

    /// Returns the AttributeSpec for this object's attribute in the given layer
    virtual SdfAttributeSpecHandle _getAttributeSpec(const SdfLayerHandle& layer)
    {
        return layer->GetAttributeAtPath(m_path);
    }

    // Helper function that will get the xformOpOrder attribute for the given primSpec and if it has a default value
    // return it via the xformOpOrder parameter. If the attribute doesn't exist it will be created and the xformOpOrder
    // parameter will be unchanged.
    // This is used by Transformers that need to modify the xformOpOrder attribute where they are passed the value of
    // the xformOpOrder attribute since it may not exist in the layer yet. But the Transformers are expected to check
    // if the xformOpOrder has been created in the layer and work with that value instead if it does exist since it may
    // have been created and modified by an earlier Transformer.
    SdfAttributeSpecHandle getOrCreateXformOpOrder(const SdfLayerHandle& layer,
                                                   SdfPrimSpecHandle primSpec,
                                                   VtArray<TfToken>& xformOpOrder)
    {
        SdfAttributeSpecHandle orderSpec =
            layer->GetAttributeAtPath(primSpec->GetPath().AppendProperty(UsdGeomTokens->xformOpOrder));
        if (!orderSpec)
        {
            // create new attribute
            orderSpec = SdfAttributeSpec::New(primSpec,
                                              UsdGeomTokens->xformOpOrder,
                                              SdfValueTypeNames->TokenArray,
                                              SdfVariabilityUniform);
        }
        // otherwise retrieve existing value
        else if (orderSpec->HasDefaultValue())
        {
            xformOpOrder = orderSpec->GetDefaultValue().Get<VtArray<TfToken>>();
        }

        return orderSpec;
    }
};

typedef std::unique_ptr<AttributeTransformer> AttributeTransformerPtr;
typedef std::vector<AttributeTransformerPtr> AttributeTransformers;


/// Derived Transformer which does the same transformations as the base class but will create the attribute in the layer
/// if it doesn't exist
class CreateAttributeTransformer : public AttributeTransformer
{
public:
    CreateAttributeTransformer(const SdfPath& path,
                               VtValue defaultValue,
                               const SdfValueTypeName& typeName,
                               SdfVariability variability)
        : AttributeTransformer(path)
        , m_defaultValue(defaultValue)
        , m_typeName(typeName)
        , m_variability(variability)
    {
    }

    ~CreateAttributeTransformer() override = default;

protected:
    VtValue m_defaultValue;
    SdfValueTypeName m_typeName;
    SdfVariability m_variability;

    /// Override base function which attempts to create the attribute in the layer if it doesn't exist
    SdfAttributeSpecHandle _getAttributeSpec(const SdfLayerHandle& layer) override
    {
        SdfAttributeSpecHandle attrSpec = layer->GetAttributeAtPath(m_path);
        // create the attribute spec if its not in the layer
        if (!attrSpec)
        {
            // get the prim spec that will own this attribute
            SdfPrimSpecHandle primSpec = layer->GetPrimAtPath(m_path.GetPrimPath());
            if (primSpec)
            {
                // create the attribute and set the default value
                attrSpec = SdfAttributeSpec::New(primSpec, m_path.GetName(), m_typeName, m_variability);
                if (attrSpec)
                {
                    attrSpec->SetDefaultValue(m_defaultValue);
                }
            }
        }
        return attrSpec;
    }
};


/// Derived Transformer which removes all xform ops from a prim and creates a single collapsed matrix xform op with
/// transformations applied
class CollapsedXformTransformer : public AttributeTransformer
{
public:
    CollapsedXformTransformer(const SdfPath& path,
                              std::unique_ptr<GfMatrix4d>&& xformDefault,
                              std::map<double, GfMatrix4d>&& xformsTimeSampled,
                              bool requiresRotation)
        : AttributeTransformer(path)
        , m_xformDefault(std::move(xformDefault))
        , m_xformsTimeSampled(std::move(xformsTimeSampled))
        , m_requiresRotation(requiresRotation)
    {
    }

    ~CollapsedXformTransformer() override = default;

    void transform(const SdfLayerHandle& layer, std::vector<std::string>& warnings) override
    {
        // get the prim spec that will own this attribute - the path in the collapsed transform case represents the prim
        // rather than an attribute
        SdfPrimSpecHandle primSpec = layer->GetPrimAtPath(m_path);
        if (!primSpec)
        {
            warnings.emplace_back("Failed to retrieve prim spec in active edit layer when collapsing transforms: " +
                                  m_path.GetAsString());
            return;
        }

        // remove existing transform ops
        for (const SdfAttributeSpecHandle& attrSpec : primSpec->GetAttributes())
        {
            if (_isXformOpAttribute(attrSpec))
            {
                primSpec->RemoveProperty(attrSpec);
            }
        }

        // transform default value and time samples
        if (m_xformDefault)
        {
            _transformMatrix4d(*m_xformDefault);
        }
        for (auto& xform : m_xformsTimeSampled)
        {
            _transformMatrix4d(xform.second);
        }

        // create the collapsed transform
        SdfAttributeSpecHandle matrixSpec =
            SdfAttributeSpec::New(primSpec, _tokens->xformOpTransform, SdfValueTypeNames->Matrix4d, SdfVariabilityVarying);
        if (m_xformDefault)
        {
            matrixSpec->SetDefaultValue(VtValue(*m_xformDefault));
        }
        if (!m_xformsTimeSampled.empty())
        {
            SdfTimeSampleMap timeSamples;
            for (auto& xform : m_xformsTimeSampled)
            {
                timeSamples.insert(std::make_pair(xform.first, VtValue(xform.second)));
            }
            matrixSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
        }

        // get or create the order attribute
        SdfAttributeSpecHandle orderSpec = layer->GetAttributeAtPath(m_path.AppendProperty(UsdGeomTokens->xformOpOrder));
        if (!orderSpec)
        {
            orderSpec = SdfAttributeSpec::New(primSpec,
                                              UsdGeomTokens->xformOpOrder,
                                              SdfValueTypeNames->TokenArray,
                                              SdfVariabilityUniform);
        }
        VtArray<TfToken> order = { _tokens->xformOpTransform };
        orderSpec->SetDefaultValue(VtValue(order));

        return;
    }

private:
    std::unique_ptr<GfMatrix4d> m_xformDefault;
    std::map<double, GfMatrix4d> m_xformsTimeSampled;
    bool m_requiresRotation;

    void _transformMatrix4d(GfMatrix4d& matrix)
    {
        if (m_doScale)
        {
            _scaleMatrix4d(matrix, m_scale);
        }
        if (m_doChangeOfBasis)
        {
            matrix = m_CoB.m_changeOfBasis * matrix * m_CoB.m_changeOfBasisInv;

            // apply rotation to the matrix to change the up axis?
            if (m_requiresRotation)
            {
                matrix = GfMatrix4d(m_CoB.m_rotation, GfVec3d(0, 0, 0)) * matrix;
            }
        }
    }
};


// Helper class used to represent a single axis rotate xformOp
class SingleAxisRotateXformOp
{
public:
    TfToken m_opName;
    size_t m_axisIndex;
    VtValue m_defaultValue;
    SdfTimeSampleMap m_timeSamples;

    SingleAxisRotateXformOp(const UsdGeomXformOp& xform)
        : m_opName(xform.GetOpName())
    {
        // resolve the axis index
        if (xform.GetOpType() == UsdGeomXformOp::TypeRotateX)
        {
            m_axisIndex = 0;
        }
        else if (xform.GetOpType() == UsdGeomXformOp::TypeRotateY)
        {
            m_axisIndex = 1;
        }
        else if (xform.GetOpType() == UsdGeomXformOp::TypeRotateZ)
        {
            m_axisIndex = 2;
        }

        // resolve the default value and time samples
        const UsdAttribute& attr = xform.GetAttr();
        // note: this will be an empty VtValue if the xformOp has no default value
        attr.Get(&m_defaultValue);
        std::vector<double> timeSamples;
        attr.GetTimeSamples(&timeSamples);
        for (double time : timeSamples)
        {
            VtValue value;
            attr.Get(&value, time);
            m_timeSamples[time] = value;
        }
    }

    bool hasDefault() const
    {
        return !m_defaultValue.IsEmpty();
    }

    bool hasTimeSample(double time) const
    {
        return m_timeSamples.find(time) != m_timeSamples.end();
    }

    template <typename T>
    T getDefaultAs() const
    {
        if (m_defaultValue.IsHolding<GfHalf>())
        {
            return static_cast<T>(m_defaultValue.UncheckedGet<GfHalf>());
        }
        if (m_defaultValue.IsHolding<float>())
        {
            return static_cast<T>(m_defaultValue.UncheckedGet<float>());
        }
        return static_cast<T>(m_defaultValue.UncheckedGet<double>());
    }

    template <typename T>
    T getTimeSampleAs(double time) const
    {
        auto findTimeSample = m_timeSamples.find(time)->second;
        if (findTimeSample.IsHolding<GfHalf>())
        {
            return static_cast<T>(findTimeSample.UncheckedGet<GfHalf>());
        }
        if (findTimeSample.IsHolding<float>())
        {
            return static_cast<T>(findTimeSample.UncheckedGet<float>());
        }
        return static_cast<T>(findTimeSample.UncheckedGet<double>());
    }
};


// Derived Transformer which handles transforming all single axis rotate attributes from a UsdGeomXformable prim at
// once and then updating the xformOpOrder attribute
class SingleAxisRotateTransformer : public AttributeTransformer
{
public:
    SingleAxisRotateTransformer(const SdfPath& primPath,
                                std::vector<SingleAxisRotateXformOp>&& xformOps,
                                const VtArray<TfToken>& xformOpOrder)
        : AttributeTransformer(primPath)
        , m_xformOps(std::move(xformOps))
        , m_xformOpOrder(xformOpOrder)
    {
    }

    ~SingleAxisRotateTransformer() override = default;

    void transform(const SdfLayerHandle& layer, std::vector<std::string>& warnings) override
    {
        SdfPrimSpecHandle primSpec = layer->GetPrimAtPath(m_path);
        if (!primSpec)
        {
            // if the PrimSpec isn't in the layer then we can safely infer that there are no xformOps to transform in
            // the layer either, so there's no work to do
            return;
        }

        // discover the needed remappings for the xformOps
        for (const auto& xformOp : m_xformOps)
        {
            findXformOpRemap(layer, xformOp);
        }

        // process the remappings and create or update the new attributes
        for (auto& remap : m_remappings)
        {
            SdfAttributeSpecHandle originalAttrSpec = remap.second.originalAttr;
            SdfAttributeSpecHandle newAttrSpec = layer->GetAttributeAtPath(m_path.AppendProperty(remap.second.newName));
            if (!newAttrSpec)
            {
                newAttrSpec = SdfAttributeSpec::New(primSpec,
                                                    remap.second.newName,
                                                    originalAttrSpec->GetTypeName(),
                                                    originalAttrSpec->GetVariability());
            }
            // set the value(s) on the new attribute
            if (!remap.second.defaultValue.IsEmpty())
            {
                newAttrSpec->SetDefaultValue(remap.second.defaultValue);
            }
            if (!remap.second.timeSamples.empty())
            {
                newAttrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(remap.second.timeSamples));
            }
        }

        // remove the original attributes that don't appear in the remapping
        for (const auto& remap : m_remappings)
        {
            bool remove = true;
            for (const auto& remap2 : m_remappings)
            {
                if (remap.first == remap2.second.newName)
                {
                    remove = false;
                    break;
                }
            }
            if (remove)
            {
                primSpec->RemoveProperty(remap.second.originalAttr);
            }
        }

        // update or create the xformOpOrder
        SdfAttributeSpecHandle orderSpec = getOrCreateXformOpOrder(layer, primSpec, m_xformOpOrder);
        VtArray<TfToken> newOrder;
        for (const TfToken& oldOp : m_xformOpOrder)
        {
            // has the xformOp been remapped?
            auto findRemap = m_remappings.find(oldOp);
            if (findRemap != m_remappings.end())
            {
                newOrder.push_back(findRemap->second.newName);
            }
            // if it hasn't been remapped but another xformOp was remapped to this then we don't want to add it to the
            // xformOpOrder since its already been added
            else
            {
                bool remappedTo = false;
                for (const auto& remap : m_remappings)
                {
                    if (oldOp == remap.second.newName)
                    {
                        remappedTo = true;
                        break;
                    }
                }
                if (!remappedTo)
                {
                    newOrder.push_back(oldOp);
                }
            }
        }
        orderSpec->SetDefaultValue(VtValue(newOrder));
    }

private:
    struct XformOpRemap
    {
        SdfAttributeSpecHandle originalAttr;
        size_t newAxisIndex;
        TfToken newName;
        VtValue defaultValue;
        SdfTimeSampleMap timeSamples;
    };

    // list of pairs of the names and axis index of the single axis rotate xformOps to transform
    std::vector<SingleAxisRotateXformOp> m_xformOps;
    // map of the original attribute name to the remapping information
    std::unordered_map<TfToken, XformOpRemap, TfToken::HashFunctor> m_remappings;
    // the xformOpOrder of the UsdGeomXformable prim these xformOps belong to
    VtArray<TfToken> m_xformOpOrder;

    bool findXformOpRemap(const SdfLayerHandle& layer, const SingleAxisRotateXformOp& xformOp)
    {
        // is the xformOp in the active edit layer?
        SdfAttributeSpecHandle attrSpec = layer->GetAttributeAtPath(m_path.AppendProperty(xformOp.m_opName));
        if (!attrSpec)
        {
            return false;
        }

        // transform the axis of this euler angle
        GfVec3d axisValues = m_CoB.m_rotation.TransformDir(AXIS[xformOp.m_axisIndex]);
        // find the first non-zero value of the new axis to determine which component is being rotated and the new value
        for (size_t i = 0; i < 3; ++i)
        {
            if (fabs(axisValues[i]) > 0.0001)
            {
                // split the current attribute name to discover if there is a suffix that need to be retained
                std::string suffix;
                const std::vector<std::string> split = TfStringSplit(xformOp.m_opName.GetString(), ":");
                for (size_t i = 2; i < split.size(); ++i)
                {
                    suffix += ":" + split[i];
                }

                // resolve the new name of the attribute
                TfToken newName;
                if (i == 0)
                {
                    newName = TfToken("xformOp:rotateX" + suffix);
                }
                else if (i == 1)
                {
                    newName = TfToken("xformOp:rotateY" + suffix);
                }
                else
                {
                    newName = TfToken("xformOp:rotateZ" + suffix);
                }

                // is the new name in the layer? if its not since we're overwriting it we need to add the new rotation
                // to the existing rotation
                SdfAttributeSpecHandle newAttrSpec = layer->GetAttributeAtPath(m_path.AppendProperty(newName));
                const SingleAxisRotateXformOp* additiveXformOp = nullptr;
                if (!newAttrSpec)
                {
                    // find the xformOp info that was initially discovered for the attribute we're overwriting
                    for (const SingleAxisRotateXformOp& existingXformOp : m_xformOps)
                    {
                        if (existingXformOp.m_opName == newName)
                        {
                            additiveXformOp = &existingXformOp;
                            break;
                        }
                    }
                }

                // get default value and time samples
                VtValue defaultValue;
                if (attrSpec->HasDefaultValue())
                {
                    if (attrSpec->GetTypeName() == SdfValueTypeNames->Half)
                    {
                        GfHalf angle =
                            attrSpec->GetDefaultValue().UncheckedGet<GfHalf>() * static_cast<GfHalf>(axisValues[i]);
                        if (additiveXformOp != nullptr && additiveXformOp->hasDefault())
                        {
                            angle += additiveXformOp->getDefaultAs<GfHalf>();
                        }
                        defaultValue = VtValue(angle);
                    }
                    else if (attrSpec->GetTypeName() == SdfValueTypeNames->Float)
                    {
                        float angle =
                            attrSpec->GetDefaultValue().UncheckedGet<float>() * static_cast<float>(axisValues[i]);
                        if (additiveXformOp != nullptr && additiveXformOp->hasDefault())
                        {
                            angle += additiveXformOp->getDefaultAs<float>();
                        }
                        defaultValue = VtValue(angle);
                    }
                    else if (attrSpec->GetTypeName() == SdfValueTypeNames->Double)
                    {
                        double angle = attrSpec->GetDefaultValue().UncheckedGet<double>() * axisValues[i];
                        if (additiveXformOp != nullptr && additiveXformOp->hasDefault())
                        {
                            angle += additiveXformOp->getDefaultAs<double>();
                        }
                        defaultValue = VtValue(angle);
                    }
                }
                SdfTimeSampleMap newTimeSamples;
                SdfTimeSampleMap timeSamples = attrSpec->GetTimeSampleMap();
                if (!timeSamples.empty())
                {
                    for (auto& timeSample : timeSamples)
                    {
                        if (attrSpec->GetTypeName() == SdfValueTypeNames->Half)
                        {
                            GfHalf angle = timeSample.second.UncheckedGet<GfHalf>() * static_cast<GfHalf>(axisValues[i]);
                            if (additiveXformOp != nullptr && additiveXformOp->hasTimeSample(timeSample.first))
                            {
                                angle += additiveXformOp->getTimeSampleAs<GfHalf>(timeSample.first);
                            }
                            newTimeSamples[timeSample.first] = angle;
                        }
                        else if (attrSpec->GetTypeName() == SdfValueTypeNames->Float)
                        {
                            float angle = timeSample.second.UncheckedGet<float>() * static_cast<float>(axisValues[i]);
                            if (additiveXformOp != nullptr && additiveXformOp->hasTimeSample(timeSample.first))
                            {
                                angle += additiveXformOp->getTimeSampleAs<float>(timeSample.first);
                            }
                            newTimeSamples[timeSample.first] = angle;
                        }
                        else if (attrSpec->GetTypeName() == SdfValueTypeNames->Double)
                        {
                            double angle = timeSample.second.UncheckedGet<double>() * axisValues[i];
                            if (additiveXformOp != nullptr && additiveXformOp->hasTimeSample(timeSample.first))
                            {
                                angle += additiveXformOp->getTimeSampleAs<double>(timeSample.first);
                            }
                            newTimeSamples[timeSample.first] = angle;
                        }
                    }
                }

                // store the remapping
                m_remappings.insert(std::make_pair(xformOp.m_opName,
                                                   XformOpRemap{ attrSpec, i, newName, defaultValue, newTimeSamples }));
                return true;
            }
        }
        return false;
    }
};


// Derived Transformer which adds a new xformOp:rotateX to the prim to correct the up axis of the prim. This is
// needed for schema prims that don't have geometry that can be rotated (e.g. camera, RectLight, Cone, etc)
class UpAxisCorrectionTransformer : public AttributeTransformer
{
public:
    UpAxisCorrectionTransformer(const SdfPath& primPath,
                                const TfToken& axisCorrectionAttrName,
                                const VtArray<TfToken>& xformOpOrder)
        : AttributeTransformer(primPath)
        , m_axisCorrectionAttrName(axisCorrectionAttrName)
        , m_xformOpOrder(xformOpOrder)
    {
    }

    ~UpAxisCorrectionTransformer() override = default;

    void transform(const SdfLayerHandle& layer, std::vector<std::string>& warnings) override
    {
        SdfPrimSpecHandle primSpec = layer->GetPrimAtPath(m_path);
        if (!primSpec)
        {
            warnings.push_back(
                "Failed to retrieve prim spec in active edit layer when adding up axis correction to xformOp: " +
                m_path.GetAsString());
            return;
        }

        // create the up axis correction attribute
        SdfAttributeSpecHandle attrSpec =
            SdfAttributeSpec::New(primSpec, m_axisCorrectionAttrName, SdfValueTypeNames->Float, SdfVariabilityVarying);
        attrSpec->SetDefaultValue(VtValue(m_CoB.m_rotation.GetAngle()));

        // write the new xformOp order - even though we were passed the xformOpOrder we need to attempt to retrieve it
        // from the layer first in case an earlier transformer has added/modified it
        SdfAttributeSpecHandle orderSpec = getOrCreateXformOpOrder(layer, primSpec, m_xformOpOrder);
        m_xformOpOrder.push_back(m_axisCorrectionAttrName);
        orderSpec->SetDefaultValue(VtValue(m_xformOpOrder));
    }

private:
    TfToken m_axisCorrectionAttrName;
    VtArray<TfToken> m_xformOpOrder;
};


/// Derived Transformer which operates on a UsdPhysicsLimitAPI prim and flips the Y and Z limits to match the new up
/// axis
class FlipPhysicsLimitTransformer : public AttributeTransformer
{
public:
    FlipPhysicsLimitTransformer(const SdfPath& primPath)
        : AttributeTransformer(primPath)
    {
    }

    ~FlipPhysicsLimitTransformer() override = default;

    void transform(const SdfLayerHandle& layer, std::vector<std::string>& warnings) override
    {
        SdfPrimSpecHandle primSpec = layer->GetPrimAtPath(m_path);
        if (!primSpec)
        {
            warnings.push_back("Failed to retrieve prim spec in active edit layer when flipping physics limits: " +
                               m_path.GetAsString());
            return;
        }

        // get the api schemas since we will need to rename them
        VtValue schemasValue = primSpec->GetInfo(_tokens->apiSchemas);
        if (schemasValue.IsHolding<SdfListOp<TfToken>>())
        {
            SdfListOp<TfToken> apiSchemas = schemasValue.UncheckedGet<SdfListOp<TfToken>>();
            // rename explicit items if this is an explicit list
            if (apiSchemas.IsExplicit())
            {
                TfTokenVector explicitItems = apiSchemas.GetExplicitItems();
                if (renameInList(explicitItems))
                {
                    apiSchemas.SetExplicitItems(explicitItems);
                }
            }
            // otherwise separately rename the prepended, appended, and deleted items
            else
            {
                TfTokenVector preprendedItems = apiSchemas.GetPrependedItems();
                if (renameInList(preprendedItems))
                {
                    apiSchemas.SetPrependedItems(preprendedItems);
                }
                TfTokenVector appendedItems = apiSchemas.GetAppendedItems();
                if (renameInList(appendedItems))
                {
                    apiSchemas.SetAppendedItems(appendedItems);
                }
                TfTokenVector deletedItems = apiSchemas.GetDeletedItems();
                if (renameInList(deletedItems))
                {
                    apiSchemas.SetDeletedItems(deletedItems);
                }
            }

            primSpec->SetInfo(_tokens->apiSchemas, VtValue(apiSchemas));
        }

        // get the specs for the limits
        SdfAttributeSpecHandle transYHighAttr =
            layer->GetAttributeAtPath(m_path.AppendProperty(_tokens->LimitTransYHigh));
        SdfAttributeSpecHandle transYLowAttr = layer->GetAttributeAtPath(m_path.AppendProperty(_tokens->LimitTransYLow));
        SdfAttributeSpecHandle transZHighAttr =
            layer->GetAttributeAtPath(m_path.AppendProperty(_tokens->LimitTransZHigh));
        SdfAttributeSpecHandle transZLowAttr = layer->GetAttributeAtPath(m_path.AppendProperty(_tokens->LimitTransZLow));
        SdfAttributeSpecHandle rotYHighAttr = layer->GetAttributeAtPath(m_path.AppendProperty(_tokens->LimitRotYHigh));
        SdfAttributeSpecHandle rotYLowAttr = layer->GetAttributeAtPath(m_path.AppendProperty(_tokens->LimitRotYLow));
        SdfAttributeSpecHandle rotZHighAttr = layer->GetAttributeAtPath(m_path.AppendProperty(_tokens->LimitRotZHigh));
        SdfAttributeSpecHandle rotZLowAttr = layer->GetAttributeAtPath(m_path.AppendProperty(_tokens->LimitRotZLow));

        // little weird, but if the both Y and Z limits are present then we need to swap the names but to do this we
        // have to use a temp name since Sdf doesn't allow a straight overwrite of an attribute name if it exists
        if (transYHighAttr && transZHighAttr)
        {
            transZHighAttr->SetName(_tokens->LimitTransZHigh.GetString() + ":temp");
        }
        if (transYLowAttr && transZLowAttr)
        {
            transZLowAttr->SetName(_tokens->LimitTransYLow.GetString() + ":temp");
        }
        if (rotYHighAttr && rotZHighAttr)
        {
            rotZHighAttr->SetName(_tokens->LimitRotZHigh.GetString() + ":temp");
        }
        if (rotYLowAttr && rotZLowAttr)
        {
            rotZLowAttr->SetName(_tokens->LimitRotZLow.GetString() + ":temp");
        }
        // do the actual renaming of the attributes
        if (transYHighAttr)
        {
            transYHighAttr->SetName(_tokens->LimitTransZHigh);
        }
        if (transYLowAttr)
        {
            transYLowAttr->SetName(_tokens->LimitTransZLow);
        }
        if (transZHighAttr)
        {
            transZHighAttr->SetName(_tokens->LimitTransYHigh);
        }
        if (transZLowAttr)
        {
            transZLowAttr->SetName(_tokens->LimitTransYLow);
        }
        if (rotYHighAttr)
        {
            rotYHighAttr->SetName(_tokens->LimitRotZHigh);
        }
        if (rotYLowAttr)
        {
            rotYLowAttr->SetName(_tokens->LimitRotZLow);
        }
        if (rotZHighAttr)
        {
            rotZHighAttr->SetName(_tokens->LimitRotYHigh);
        }
        if (rotZLowAttr)
        {
            rotZLowAttr->SetName(_tokens->LimitRotYLow);
        }
    }

private:
    // renames the api schemas in the list by flipping Y and Z
    bool renameInList(TfTokenVector& list)
    {
        // how schemas need to be renamed
        static const std::vector<std::pair<TfToken, TfToken>> renamePairs = {
            { _tokens->PhysicsLimitAPITransY, _tokens->PhysicsLimitAPITransZ },
            { _tokens->PhysicsLimitAPITransZ, _tokens->PhysicsLimitAPITransY },
            { _tokens->PhysicsLimitAPIRotY, _tokens->PhysicsLimitAPIRotZ },
            { _tokens->PhysicsLimitAPIRotZ, _tokens->PhysicsLimitAPIRotY }
        };

        bool renamed = false;
        for (TfToken& token : list)
        {
            for (const auto& pair : renamePairs)
            {
                if (token == pair.first)
                {
                    token = pair.second;
                    renamed = true;
                    break;
                }
            }
        }
        return renamed;
    }
};


/// Functor for traversing the layer and determine which attributes need to be scaled.
class LayerTraversalFunctor
{
public:
    LayerTraversalFunctor(const UsdStagePtr& stage,
                          const SdfLayerHandle& layer,
                          bool doScale,
                          double scale,
                          bool doChangeOfBasis,
                          const ChangeOfBasisPrecompute& changeOfBasis,
                          bool collapseXforms,
                          bool ignoreKitCameras,
                          AttributeTransformers& attributeTransformers,
                          std::vector<std::string>& warnings)
        : m_stage(stage)
        , m_layer(layer)
        , m_doScale(doScale)
        , m_scale(scale)
        , m_doChangeOfBasis(doChangeOfBasis)
        , m_CoB(changeOfBasis)
        , m_collapseXforms(collapseXforms)
        , m_ignoreKitCameras(ignoreKitCameras)
        , m_attributeTransformers(attributeTransformers)
        , m_warnings(warnings)
    {
    }

    /// functor operator called by SdfLayer::Traverse
    void operator()(const SdfPath& path)
    {
        m_primPath = path;

        // only traverse prims at this level
        if (!m_primPath.IsPrimPath())
        {
            return;
        }

        // check for special case kit viewport cameras if we're ignoring them and skip
        static const std::set<SdfPath> kitCameraPaths = { SdfPath("/OmniverseKit_Persp"),
                                                          SdfPath("/OmniverseKit_Front"),
                                                          SdfPath("/OmniverseKit_Top"),
                                                          SdfPath("/OmniverseKit_Right") };
        if (m_ignoreKitCameras && kitCameraPaths.find(m_primPath) != kitCameraPaths.end())
        {
            return;
        }

        // retrieve the UsdPrim from the stage for this path - we strip variants because initially we just want to
        // inspect prims or the prim that a variant controls.
        // note: there are potential cases where this doesn't work: if the stage's composition has changed the paths of
        //       the prims in the active edit layer. Currently we don't have a solution for this.
        UsdPrim prim = m_stage->GetPrimAtPath(m_primPath.StripAllVariantSelections());
        if (!prim)
        {
            m_warnings.emplace_back("Failed to retrieve prim in stage discovered in edit layer at path: " +
                                    m_primPath.StripAllVariantSelections().GetAsString());
            return;
        }

        // determine if we should create schema attributes for this prim if the attributes are unauthored. The criteria
        // for this is if the prim is in the edit layer, if the prim spec has a def specifier, and if the prim has an
        // authored typename
        SdfPrimSpecHandle primSpec = m_layer->GetPrimAtPath(m_primPath);
        m_createIfUnauthored = primSpec && primSpec->GetSpecifier() == SdfSpecifierDef && prim.HasAuthoredTypeName();

        m_processedScale.clear();
        m_processedChangeOfBasis.clear();

        // whether this prim requires additional rotation added to correct its up axis
        bool requiresRotation = false;
        std::unique_ptr<GfMatrix4d> xformDefault(nullptr);
        std::map<double, GfMatrix4d> xformsTimeSampled;

        // -- check schemas --
        // Process schema attributes that need scaling and discover prims that we have to rotate using an xform. For
        // example a Cone prim only has a height and radius attribute so we need to rotate the xform to change the up
        // axis. Some schemas (like Cone) have an "axis" attribute which defines the direction the geometry faces a long
        // the positive axis. We can't use this since in many cases we need to face the geometry along the negative
        // axis.
        if (UsdGeomBoundable boundable = UsdGeomBoundable(prim))
        {
            processSchemaAttribute(boundable.GetExtentAttr(), true);
        }
        if (UsdGeomModelAPI modelApi = UsdGeomModelAPI(prim))
        {
            processSchemaAttribute(modelApi.GetExtentsHintAttr(), true);
        }
        if (UsdGeomCamera camera = UsdGeomCamera(prim))
        {
            // note: we only scale the clipping range of the camera, any other attributes relating to lens or filmback
            //       are measured in scene units so should *not* be scaled
            processSchemaAttribute(camera.GetClippingRangeAttr());
            requiresRotation = m_doChangeOfBasis;
        }
        if (UsdLuxCylinderLight cylinderLight = UsdLuxCylinderLight(prim))
        {
            processSchemaAttribute(cylinderLight.GetLengthAttr());
            processSchemaAttribute(cylinderLight.GetRadiusAttr());
            requiresRotation = m_doChangeOfBasis;
        }
        if (UsdLuxDiskLight diskLight = UsdLuxDiskLight(prim))
        {
            processSchemaAttribute(diskLight.GetRadiusAttr());
            requiresRotation = m_doChangeOfBasis;
        }
        if (UsdLuxDomeLight domeLight = UsdLuxDomeLight(prim))
        {
            processSchemaAttribute(domeLight.GetGuideRadiusAttr());
            requiresRotation = m_doChangeOfBasis;
        }
        if (UsdLuxRectLight rectLight = UsdLuxRectLight(prim))
        {
            processSchemaAttribute(rectLight.GetWidthAttr());
            processSchemaAttribute(rectLight.GetHeightAttr());
            requiresRotation = m_doChangeOfBasis;
        }
        if (UsdLuxSphereLight sphereLight = UsdLuxSphereLight(prim))
        {
            processSchemaAttribute(sphereLight.GetRadiusAttr());
        }
        if (UsdLuxDistantLight DistantLight = UsdLuxDistantLight(prim))
        {
            requiresRotation = m_doChangeOfBasis;
        }
        if (UsdGeomCapsule capsule = UsdGeomCapsule(prim))
        {
            processSchemaAttribute(capsule.GetHeightAttr());
            processSchemaAttribute(capsule.GetRadiusAttr());
            requiresRotation = m_doChangeOfBasis;
        }
        if (UsdGeomCone cone = UsdGeomCone(prim))
        {
            processSchemaAttribute(cone.GetHeightAttr());
            processSchemaAttribute(cone.GetRadiusAttr());
            requiresRotation = m_doChangeOfBasis;
        }
        if (UsdGeomCube cube = UsdGeomCube(prim))
        {
            processSchemaAttribute(cube.GetSizeAttr());
        }
        if (UsdGeomCylinder cylinder = UsdGeomCylinder(prim))
        {
            processSchemaAttribute(cylinder.GetHeightAttr());
            processSchemaAttribute(cylinder.GetRadiusAttr());
            requiresRotation = m_doChangeOfBasis;
        }
        if (UsdGeomPlane plane = UsdGeomPlane(prim))
        {
            processSchemaAttribute(plane.GetWidthAttr());
            processSchemaAttribute(plane.GetLengthAttr());
            requiresRotation = m_doChangeOfBasis;
        }
        if (UsdGeomSphere sphere = UsdGeomSphere(prim))
        {
            processSchemaAttribute(sphere.GetRadiusAttr());
        }
        if (UsdGeomCurves curves = UsdGeomCurves(prim))
        {
            processSchemaAttribute(curves.GetWidthsAttr());
        }
        if (UsdSkelAnimation anim = UsdSkelAnimation(prim))
        {
            processSchemaAttribute(anim.GetTranslationsAttr(), true);
        }
        if (UsdPhysicsMassAPI massApi = UsdPhysicsMassAPI(prim))
        {
            // don't touch density if it is 0.0
            UsdAttribute densityAttr = massApi.GetDensityAttr();
            float densityValue;
            densityAttr.Get(&densityValue);
            if (densityValue >= std::numeric_limits<float>::epsilon())
            {
                // density is a special case, it gets cubically smaller as the scale increases
                // This is because density * volume = mass, and we want the mass to remain constant after changing the
                // units
                processSchemaAttribute(densityAttr, false, -3.0);
            }
        }

        // handle UsdPhysicsLimitAPI schemas - we need to track if any Y or Z limits are present so they can be flipped
        // if the up axis is changed
        bool flipLimits = false;

        // find translation limits, these need to be scaled
        static const TfTokenVector limitApiTransNames = { _tokens->LimitIdentifierTransX,
                                                          _tokens->LimitIdentifierTransY,
                                                          _tokens->LimitIdentifierTransZ };
        for (const TfToken& limitApiName : limitApiTransNames)
        {
            if (UsdPhysicsLimitAPI limitApi = UsdPhysicsLimitAPI(prim, limitApiName))
            {
                if (limitApiName == _tokens->LimitIdentifierTransY || limitApiName == _tokens->LimitIdentifierTransZ)
                {
                    flipLimits = true;
                }

                // ensure high and low values are scaled
                processSchemaAttribute(limitApi.GetHighAttr());
                processSchemaAttribute(limitApi.GetLowAttr());
            }
        }

        // find rotation limits, don't need to scale but if we have Y or Z limits we need to flip them
        if (m_doChangeOfBasis && !flipLimits)
        {
            static const TfTokenVector limitApiRotNames = { _tokens->LimitIdentifierRotY, _tokens->LimitIdentifierRotZ };
            for (const TfToken& limitApiName : limitApiRotNames)
            {
                if (UsdPhysicsLimitAPI limitApi = UsdPhysicsLimitAPI(prim, limitApiName))
                {
                    flipLimits = true;
                    break;
                }
            }
        }

        // add a physics limit flip transformer?
        if (m_doChangeOfBasis && flipLimits)
        {
            AttributeTransformerPtr transformer(new FlipPhysicsLimitTransformer(m_primPath));
            m_attributeTransformers.push_back(std::move(transformer));
        }

        // handle change of basis for xformables
        bool xformsCollapsed = false;
        if (m_doChangeOfBasis)
        {
            if (UsdGeomXformable xformable = UsdGeomXformable(prim))
            {
                bool resetsXformStack;
                if (m_collapseXforms)
                {
                    // collapse transforms into a single matrix if we're changing the up axis and there are ordered
                    // xformOps in the active edit layer
                    std::vector<UsdGeomXformOp> xforms = xformable.GetOrderedXformOps(&resetsXformStack);
                    bool hasDefaultValue = false;
                    std::unordered_set<double> timecodes;
                    for (const UsdGeomXformOp& xform : xforms)
                    {
                        // build the attribute path for this xformOp - we need to rebuild the path in case the original
                        // prim path includes variants (the UsdGeomXformable will be looking at the non-variant version
                        // of the prim)
                        const SdfPath attrPath = m_primPath.AppendProperty(xform.GetAttr().GetName());

                        // get the attribute spec for this xformOp attribute
                        SdfAttributeSpecHandle attrSpec = m_layer->GetAttributeAtPath(attrPath);
                        if (m_layer->GetAttributeAtPath(attrPath))
                        {
                            // at least one xformOp is in the active edit layer - collapse xforms
                            xformsCollapsed = true;

                            // at least one xform has a default value - we will need to compute the collapsed matrix for
                            // the default value
                            if (attrSpec->HasDefaultValue())
                            {
                                hasDefaultValue = true;
                            }
                        }

                        // update the set of time samples we need to compute collapsed matrices for
                        std::vector<double> timeSamples;
                        xform.GetTimeSamples(&timeSamples);
                        for (double timeSample : timeSamples)
                        {
                            timecodes.insert(timeSample);
                        }
                    }

                    // compute the default value collapsed matrix?
                    if (hasDefaultValue)
                    {
                        xformDefault.reset(new GfMatrix4d(1.0));
                        xformable.GetLocalTransformation(xformDefault.get(), &resetsXformStack);
                    }

                    // compute the timesampled collapsed matrices
                    for (double timecode : timecodes)
                    {
                        GfMatrix4d matrix;
                        xformable.GetLocalTransformation(&matrix, &resetsXformStack, timecode);
                        xformsTimeSampled.insert(std::make_pair(timecode, matrix));
                    }
                }
                // otherwise we need to look for single axis rotate attributes since they need to be handled as a fairly
                // complicated special case
                else
                {
                    std::vector<SingleAxisRotateXformOp> xformOps;
                    std::vector<UsdGeomXformOp> xforms = xformable.GetOrderedXformOps(&resetsXformStack);
                    for (const UsdGeomXformOp& xform : xforms)
                    {
                        switch (xform.GetOpType())
                        {
                        case UsdGeomXformOp::TypeRotateX:
                        case UsdGeomXformOp::TypeRotateY:
                        case UsdGeomXformOp::TypeRotateZ:
                            xformOps.push_back(SingleAxisRotateXformOp(xform));
                            m_processedChangeOfBasis.insert(xform.GetAttr().GetName());
                            break;
                        default:
                            // do nothing
                            break;
                        }
                    }

                    // create the single axis transformer if needed
                    if (!xformOps.empty())
                    {
                        // get the xformOpOrder
                        VtArray<TfToken> xformOpOrder;
                        UsdAttribute xformOpOrderAttr = xformable.GetXformOpOrderAttr();
                        if (xformOpOrderAttr.IsValid())
                        {
                            xformOpOrderAttr.Get(&xformOpOrder);
                        }

                        AttributeTransformerPtr transformer(
                            new SingleAxisRotateTransformer(m_primPath, std::move(xformOps), xformOpOrder));
                        transformer->setChangeOfBasis(m_CoB);
                        m_attributeTransformers.push_back(std::move(transformer));
                    }
                }
            }
        }

        // create a collapsed xform transformer
        if (xformsCollapsed || (m_collapseXforms && requiresRotation))
        {
            // if we need to add additional rotation to the prim but the was no default xform matrix, use an identity
            // matrix
            if (requiresRotation && !xformDefault)
            {
                xformDefault.reset(new GfMatrix4d(1.0));
            }

            AttributeTransformerPtr transformer(new CollapsedXformTransformer(m_primPath,
                                                                              std::move(xformDefault),
                                                                              std::move(xformsTimeSampled),
                                                                              requiresRotation));
            if (m_doScale)
            {
                transformer->setScale(m_scale);
            }
            if (m_doChangeOfBasis)
            {
                transformer->setChangeOfBasis(m_CoB);
            }
            m_attributeTransformers.push_back(std::move(transformer));
        }
        // otherwise create an up axis correction transformer if this prim is concrete in the active layer
        else if (requiresRotation && primSpec->GetSpecifier() == SdfSpecifierDef)
        {
            // resolve the name of the up axis correction attribute - this needs to be done at this point since at
            // the time we write the attribute we're in an SdfChangeBlock so can only read from the active edit layer
            TfToken axisCorrectionAttrName = _tokens->xformOpRotateUpAxisCorrection;
            size_t suffix = 1;
            while (prim.HasAttribute(axisCorrectionAttrName))
            {
                axisCorrectionAttrName =
                    TfToken(_tokens->xformOpRotateUpAxisCorrection.GetString() + std::to_string(suffix));
                ++suffix;
            }

            // get the xformOpOrder since this will need updating after adding the up axis correction xform
            VtArray<TfToken> xformOpOrder;
            if (UsdGeomXformable xformable = UsdGeomXformable(prim))
            {
                UsdAttribute xformOpOrderAttr = xformable.GetXformOpOrderAttr();
                if (xformOpOrderAttr.IsValid())
                {
                    xformOpOrderAttr.Get(&xformOpOrder);
                }
            }

            // create the transformer
            AttributeTransformerPtr transformer(
                new UpAxisCorrectionTransformer(m_primPath, axisCorrectionAttrName, xformOpOrder));
            transformer->setChangeOfBasis(m_CoB);
            m_attributeTransformers.push_back(std::move(transformer));
        }

        // only process arbitrary attributes if the prim is in the edit layer
        if (!primSpec)
        {
            return;
        }

        // attribute types to scale
        static const std::unordered_set<SdfValueTypeName, SdfValueTypeNameHashFunctor> scaleTypeWhitelist = {
            SdfValueTypeNames->Matrix4d,      SdfValueTypeNames->Matrix4dArray, SdfValueTypeNames->Point3h,
            SdfValueTypeNames->Point3f,       SdfValueTypeNames->Point3d,       SdfValueTypeNames->Vector3h,
            SdfValueTypeNames->Vector3f,      SdfValueTypeNames->Vector3d,      SdfValueTypeNames->Point3hArray,
            SdfValueTypeNames->Point3fArray,  SdfValueTypeNames->Point3dArray,  SdfValueTypeNames->Vector3hArray,
            SdfValueTypeNames->Vector3fArray, SdfValueTypeNames->Vector3dArray
        };
        // attribute types to rotate
        static const std::unordered_set<SdfValueTypeName, SdfValueTypeNameHashFunctor> changeOfBasisTypeWhitelist = {
            SdfValueTypeNames->Matrix3d,      SdfValueTypeNames->Matrix3dArray, SdfValueTypeNames->Matrix4d,
            SdfValueTypeNames->Matrix4dArray, SdfValueTypeNames->Quath,         SdfValueTypeNames->QuathArray,
            SdfValueTypeNames->Quatf,         SdfValueTypeNames->QuatfArray,    SdfValueTypeNames->Quatd,
            SdfValueTypeNames->QuatdArray,    SdfValueTypeNames->Point3h,       SdfValueTypeNames->Point3f,
            SdfValueTypeNames->Point3d,       SdfValueTypeNames->Vector3h,      SdfValueTypeNames->Vector3f,
            SdfValueTypeNames->Vector3d,      SdfValueTypeNames->Normal3f,      SdfValueTypeNames->Normal3d,
            SdfValueTypeNames->Point3hArray,  SdfValueTypeNames->Point3fArray,  SdfValueTypeNames->Point3dArray,
            SdfValueTypeNames->Vector3hArray, SdfValueTypeNames->Vector3fArray, SdfValueTypeNames->Vector3dArray,
            SdfValueTypeNames->Normal3fArray, SdfValueTypeNames->Normal3dArray
        };

        // attribute names that shouldn't be scaled
        static const std::unordered_set<std::string> scaleBlacklist = { "physics:angularVelocity" };
        // attribute names that shouldn't have their basis changed
        static const std::unordered_set<std::string> changeOfBasisBlacklist = { "omni:kit:centerOfInterest" };

        // Traverse the prim spec's attributes and check they need transforming
        // Note: this catches things like xforms, point-based geometry, PointInstancers, and the majority of curve
        //       attributes
        for (const SdfAttributeSpecHandle& attrSpec : primSpec->GetAttributes())
        {
            // ignore any xform attributes if xforms have been collapsed
            if (xformsCollapsed && _isXformOpAttribute(attrSpec))
            {
                continue;
            }

            bool performScale = false;
            bool performChangeOfBasis = false;

            // determine if we need to scale this attribute
            if (m_doScale && m_processedScale.find(attrSpec->GetName()) == m_processedScale.end() &&
                scaleBlacklist.find(attrSpec->GetName()) == scaleBlacklist.end())
            {
                // translate attributes need scaling regardless of their type - otherwise check typewhite list
                performScale = _isTranslateAttribute(attrSpec) ||
                               scaleTypeWhitelist.find(attrSpec->GetTypeName()) != scaleTypeWhitelist.end();
            }
            // determine if we need to change the basis of this attribute
            if (m_doChangeOfBasis &&
                m_processedChangeOfBasis.find(attrSpec->GetName()) == m_processedChangeOfBasis.end() &&
                changeOfBasisBlacklist.find(attrSpec->GetName()) == changeOfBasisBlacklist.end())
            {
                // xformOps need changing basis regardless of their type
                performChangeOfBasis =
                    _isXformOpAttribute(attrSpec) ||
                    changeOfBasisTypeWhitelist.find(attrSpec->GetTypeName()) != changeOfBasisTypeWhitelist.end();
            }

            // any work to do?
            if (performScale || performChangeOfBasis)
            {
                // create the transformer and set the scale and rotation
                AttributeTransformerPtr transformer(new AttributeTransformer(attrSpec->GetPath()));
                if (performScale)
                {
                    transformer->setScale(m_scale);
                }
                if (performChangeOfBasis)
                {
                    transformer->setChangeOfBasis(m_CoB);
                }
                m_attributeTransformers.push_back(std::move(transformer));
            }
        }
    }

private:
    UsdStagePtr m_stage;
    SdfLayerHandle m_layer;
    bool m_doScale;
    double m_scale;
    bool m_doChangeOfBasis;
    ChangeOfBasisPrecompute m_CoB;
    bool m_collapseXforms;
    bool m_ignoreKitCameras;
    AttributeTransformers& m_attributeTransformers;
    std::vector<std::string>& m_warnings;
    // the current prim path being processed
    SdfPath m_primPath;
    // marks whether we allow the creation of attributes for the current prim
    bool m_createIfUnauthored;
    // attribute names that have been processed as scale for the current prim
    std::unordered_set<std::string> m_processedScale;
    // attribute names that have been processed as change of basis for the current prim
    std::unordered_set<std::string> m_processedChangeOfBasis;

    /// Utility function which determines if a schema attribute needs scaling - and whether it should be created in the
    /// layer if it doesn't exist
    void processSchemaAttribute(UsdAttribute attr, bool changeOfBasis = false, double scaleExponent = 1.0)
    {
        // determine the work to do
        bool doScale = m_doScale && m_processedScale.find(attr.GetName()) == m_processedScale.end();
        bool doChangeOfBasis = changeOfBasis && m_doChangeOfBasis &&
                               m_processedChangeOfBasis.find(attr.GetName()) == m_processedChangeOfBasis.end();

        // no work required
        if (!doScale && !doChangeOfBasis)
        {
            return;
        }

        // resolve the path to the attribute. note: we don't use attr.GetPath() because the prim path might be a variant
        // path, so we need to construct the full path
        const SdfPath attrPath = m_primPath.AppendProperty(attr.GetName());

        // check if the attribute has an authored value - in which case we don't want to create it in the edit layer if
        // it doesn't exist. This is because if it doesn't exist but has an authored value it means there's an over on
        // this attribute (probably from a variant) and creating this attribute in the edit layer can override that over
        // - changing the state of the stage
        AttributeTransformerPtr transformer = nullptr;
        if (attr.HasAuthoredValue() || !m_createIfUnauthored)
        {
            transformer.reset(new AttributeTransformer(attrPath));
        }
        // otherwise if there is no authored value we pass the information to the transformer needed to create the
        // attribute in the layer if it doesn't exist. This is because schemas have default values for their attributes
        // that don't need to exist in the stage, but we need to create these attributes and scale them in the resulting
        // stage so it is properly scaled
        else
        {
            VtValue value;
            attr.Get(&value);
            transformer.reset(new CreateAttributeTransformer(attrPath, value, attr.GetTypeName(), attr.GetVariability()));
        }

        // set transformer
        if (doScale)
        {
            transformer->setScale(pow(m_scale, scaleExponent));
            m_processedScale.insert(attr.GetName());
        }
        if (doChangeOfBasis)
        {
            transformer->setChangeOfBasis(m_CoB);
            m_processedChangeOfBasis.insert(attr.GetName());
        }
        m_attributeTransformers.push_back(std::move(transformer));
    }
};


EditStageMetricsOperation::EditStageMetricsOperation()
    : Operation("editStageMetrics",
                "Edit Stage Metrics",
                "This operation changes the ``metersPerUnit`` and/or ``upAxis`` of a stage's active edit target layer "
                "by updating the layer's metadata and applying relevant transformations to attributes that represent "
                "world space units so that they reflect the new ``metersPerUnit/upAxis``."
                "The operation is designed to only modify attributes that represent a world space value in the stage's "
                "active edit layer. This means prims/attributes that exist in the scene from external references or "
                "sublayers will not be affected by the operation.\n"
                "An overview of some specifics about how the operation will affect attributes or xformOps in the "
                "stage:\n"
                "- When changing the ``metersPerUnit`` of prims that are a defined schema that have inferred "
                "attributes values that don't need to be defined. For example a Cube prim has a ``size`` attribute "
                "that does not need to be defined, and if it is not the cube will have a value of ``2.0``. When "
                "changing the ``metersPerUnit``, the operation needs to create this ``size`` attribute in order to "
                "scale its inferred value of ``2.0``. These inferred attributes will only be created if they represent "
                "world space values and the prim of the attribute exists as a concrete ``def`` in the active edit "
                "layer.\n"
                "- When changing the ``upAxis`` of prims that don't have geometry that can be rotated, the operation "
                "will add an additional `xformOp:rotateX:upAxisCorrection` attribute to correct the rotation of the "
                "prim.\n"
                "- When changing the ``upAxis`` of transforms and collapseXforms is enabled, the Edit Stage Metrics "
                "operation will collapse a prim's ``xformOp`` stack into a single matrix ``xformOp``. This creates a "
                "few cases with surprising behavior, for example if the edit stage layer contains an ``over`` on a "
                "single ``xformOp`` in a stack of ``xformOps`` on a prim in the underlying sublayer/reference, this "
                "will cause the entire ``xformOp`` stack to have its up axis transformed even though only a part of "
                "the stack exists in the active edit layer.\n")
{
    addArgument(
        "metersPerUnit",
        "Meters Per Unit",
        kDisplayTypeFloatPresets,
        "The stage's new meters per unit, where a value of 0.0 represents no change to the stage's meters per units",
        m_metersPerUnit)
        .setFloatPresets(_getDefaultLinearUnitsPresets());

    addArgument("upAxis", "Up Axis", kDisplayTypeEnum, "The stage's new up axis", m_upAxis)
        .setEnumValues<UpAxis>({
            { UpAxis::eNone, "None" },
            { UpAxis::eY, "Y" },
            { UpAxis::eZ, "Z" },
        });

    addArgument("collapseXforms",
                "Collapse Xforms",
                kDisplayTypeBool,
                "Collapse prim's xformOps into a single matrix when changing up axis",
                m_collapseXforms);

    addArgument(
        "ignoreKitCameras",
        "Ignore Kit Cameras",
        kDisplayTypeBool,
        "Whether to ignore the special Kit viewport cameras (such as persp, front, top, right) when changing stage metrics.\n Note: in UI mode the viewport applies its own correction to the viewport cameras, but in batch mode this should be disabled to ensure the cameras are corrected.",
        m_ignoreKitCameras);
}


EditStageMetricsOperation::~EditStageMetricsOperation() = default;


std::string EditStageMetricsOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion EditStageMetricsOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string EditStageMetricsOperation::getCategory() const
{
    return s_category;
}


std::string EditStageMetricsOperation::getDisplayGroup() const
{
    return s_displayGroupStage;
}


OperationResult EditStageMetricsOperation::executeImpl()
{
    // get stage information
    const UsdStagePtr& stage = getUsdStage();
    SdfLayerHandle layer = stage->GetEditTarget().GetLayer();
    UsdStageRefPtr layerStage = UsdStage::Open(layer);
    UsdPrim layerRoot = stage->GetPseudoRoot();

    // determine if there is any scaling work to do
    bool doScale = false;
    double scale = 0.0;
    // scaling on?
    if (fabs(m_metersPerUnit) > std::numeric_limits<double>::epsilon())
    {
        // get the current meters per unit, and set the new value
        double currentMetersPerUnit = UsdGeomGetStageMetersPerUnit(layerStage);
        UsdGeomSetStageMetersPerUnit(layerStage, m_metersPerUnit);

        // determine the scaling that is required
        scale = currentMetersPerUnit / m_metersPerUnit;
        if (fabs(scale - 1.0) > std::numeric_limits<double>::epsilon())
        {
            // we need to perform scaling - layer isn't already in the requested meters per unit
            doScale = true;

            SO_LOG_INFO("Changing stage metersPerUnit from %f to %f", currentMetersPerUnit, m_metersPerUnit);
        }
    }

    // determine if there is any change of basis work to do
    bool doChangeOfBasis = false;
    ChangeOfBasisPrecompute CoB;
    // up axis on?
    if (m_upAxis != UpAxis::eNone)
    {
        static const TfToken upAxisKey("upAxis");

        // get the current up axis of the layer
        TfToken currentUpAxisToken = UsdGeomGetStageUpAxis(layerStage);
        // resolve current axis from token to enum
        UpAxis currentUpAxis = UpAxis::eNone;
        const std::string currentUpAxisStr = currentUpAxisToken.GetString();
        if (currentUpAxisToken == UsdGeomTokens->y)
        {
            currentUpAxis = UpAxis::eY;
        }
        else if (currentUpAxisToken == UsdGeomTokens->z)
        {
            currentUpAxis = UpAxis::eZ;
        }

        // resolve the change of basis and rotation required and set the new up axis on the layer
        std::string newUpAxisStr;
        if (currentUpAxis == UpAxis::eY && m_upAxis == UpAxis::eZ)
        {
            UsdGeomSetStageUpAxis(layerStage, UsdGeomTokens->z);
            doChangeOfBasis = true;
            CoB.YtoZ();
            newUpAxisStr = "Z";
        }
        else if (currentUpAxis == UpAxis::eZ && m_upAxis == UpAxis::eY)
        {
            UsdGeomSetStageUpAxis(layerStage, UsdGeomTokens->y);
            doChangeOfBasis = true;
            CoB.ZtoY();
            newUpAxisStr = "Y";
        }

        if (doChangeOfBasis)
        {
            SO_LOG_INFO("Changing stage upAxis from %s to %s", currentUpAxisStr.c_str(), newUpAxisStr.c_str());
        }
    }

    // no work to do?
    if (!doScale && !doChangeOfBasis)
    {
        return { true };
    }

    // perform read stage traversal to scale the attributes
    AttributeTransformers attributeTransformers;
    std::vector<std::string> warnings;
    layer->Traverse(layerRoot.GetPath(),
                    LayerTraversalFunctor(stage,
                                          layer,
                                          doScale,
                                          scale,
                                          doChangeOfBasis,
                                          CoB,
                                          m_collapseXforms,
                                          m_ignoreKitCameras,
                                          attributeTransformers,
                                          warnings));

    // log any warnings that were generated during the traversal
    for (const std::string& message : warnings)
    {
        SO_LOG_WARN(message.c_str());
    }
    warnings.clear();

    // perform write stage where we actually transform attributes in the SdfLayer
    SdfChangeBlock changeBlock;
    for (AttributeTransformerPtr& attributeTransformer : attributeTransformers)
    {
        attributeTransformer->transform(layer, warnings);
    }
    for (const std::string& message : warnings)
    {
        SO_LOG_WARN(message.c_str());
    }

    return { true };
}


} // namespace omni::scene::optimizer
