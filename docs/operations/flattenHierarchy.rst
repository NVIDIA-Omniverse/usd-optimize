.. AUTO GENERATED FILE - DO NOT EDIT

=================
Flatten Hierarchy
=================

**Key**: ``flattenHierarchy``

Finds any Xforms in a stage that are redundant and removes them, in order to reduce prim count. This is typically an Xform that has a single Xform underneath it, or chains of single Xforms.

Certain conditions prevent an Xform from being removed. This includes Xforms that have multiple children, in order to retain some semblance of scene layout. Also Xforms that have a relationship (for example a material binding) or something that has a relationship targeting them (e.g. a material).
Xforms that have time samples are not removed. Only Xforms in the current edit target are considered, any external references will be skipped.

Xforms that are referenced (for example as an instance) must also retain their original path, however Xforms underneath them can potentially be removed.

Arguments
---------

Paths To Process
^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Identity Only
^^^^^^^^^^^^^

Only remove Xforms that do not contribute any transformation values to the hierarchy

- Name: ``identity``
- Type: ``bool``
- Default Value: ``False``

