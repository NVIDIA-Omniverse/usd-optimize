.. AUTO GENERATED FILE - DO NOT EDIT

==================
Edit Stage Metrics
==================

**Key**: ``editStageMetrics``

This operation changes the ``metersPerUnit`` and/or ``upAxis`` of a stage's active edit target layer by updating the layer's metadata and applying relevant transformations to attributes that represent world space units so that they reflect the new ``metersPerUnit/upAxis``. The operation is designed to only modify attributes that represent a world space value in the stage's active edit layer. This means prims/attributes that exist in the scene from external references or sublayers will not be affected by the operation.

An overview of some specifics about how the operation will affect attributes or xformOps in the stage:

- When changing the ``metersPerUnit`` of prims that are a defined schema that have inferred attributes values that don't need to be defined. For example a Cube prim has a ``size`` attribute that does not need to be defined, and if it is not the cube will have a value of ``2.0``. When changing the ``metersPerUnit``, the operation needs to create this ``size`` attribute in order to scale its inferred value of ``2.0``. These inferred attributes will only be created if they represent world space values and the prim of the attribute exists as a concrete ``def`` in the active edit layer.
- When changing the ``upAxis`` of prims that don't have geometry that can be rotated, the operation will add an additional `xformOp:rotateX:upAxisCorrection` attribute to correct the rotation of the prim.
- When changing the ``upAxis`` of transforms and collapseXforms is enabled, the Edit Stage Metrics operation will collapse a prim's ``xformOp`` stack into a single matrix ``xformOp``. This creates a few cases with surprising behavior, for example if the edit stage layer contains an ``over`` on a single ``xformOp`` in a stack of ``xformOps`` on a prim in the underlying sublayer/reference, this will cause the entire ``xformOp`` stack to have its up axis transformed even though only a part of the stack exists in the active edit layer.
- When ``stopAtSkeletonRoot`` is enabled, the operation stops scaling and changing the basis at the root prim (``SkelRoot``) of a skeleton. Rather than processing every prim in the skeleton hierarchy individually, a single scale and/or rotation ``xformOp`` is prepended to the skeleton root's xform as the outermost transform so that the metrics correction is applied to the entire skeleton at once, and the prims below the skeleton root are left unprocessed.


Arguments
---------

Meters Per Unit
^^^^^^^^^^^^^^^

The stage's new meters per unit, where a value of 0.0 represents no change to the stage's meters per units

- Name: ``metersPerUnit``
- Type: ``float``
- Default Value: ``0.01``

Up Axis
^^^^^^^

The stage's new up axis

- Name: ``upAxis``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: None``
  - ``1: Y``
  - ``2: Z``


Collapse Xforms
^^^^^^^^^^^^^^^

Collapse prim's xformOps into a single matrix when changing up axis

- Name: ``collapseXforms``
- Type: ``bool``
- Default Value: ``False``

Ignore Kit Cameras
^^^^^^^^^^^^^^^^^^

Whether to ignore the special Kit viewport cameras (such as persp, front, top, right) when changing stage metrics. Note: in UI mode the viewport applies its own correction to the viewport cameras, but in batch mode this should be disabled to ensure the cameras are corrected.

- Name: ``ignoreKitCameras``
- Type: ``bool``
- Default Value: ``True``

Stop At Skeleton Root
^^^^^^^^^^^^^^^^^^^^^

Whether to stop scale and change of basis processing at the root prim (``SkelRoot``) of a skeleton. When enabled, instead of processing every prim in the skeleton hierarchy individually, a single scale and/or rotation xformOp is applied to the skeleton root's xform and the prims below it are left unprocessed.

- Name: ``stopAtSkeletonRoot``
- Type: ``bool``
- Default Value: ``False``

