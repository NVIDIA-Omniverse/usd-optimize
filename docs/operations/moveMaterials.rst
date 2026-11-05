.. AUTO GENERATED FILE - DO NOT EDIT

==============
Move Materials
==============

**Key**: ``moveMaterials``

.. Note:: **Python-hosted operation**: available through the ``usd-optimize`` Python wheel / bindings, but not the standalone ``usdOptimize`` CLI (which does not initialize a Python interpreter).

Moves all materials under a prim location such as "/World/Looks" and can also set the root prim of the path (e.g. "/World") as the default prim

Arguments
---------

Materials Root
^^^^^^^^^^^^^^

The prim path that all materials will be moved under.

- Name: ``materialsPath``
- Type: ``str``
- Default Value: ``"/World/Looks"``

Make Root Default
^^^^^^^^^^^^^^^^^

Whether the root of materialsPath should be set as the default prim if the stage has no defaultPrim set.

- Name: ``makeRootDefault``
- Type: ``bool``
- Default Value: ``True``

