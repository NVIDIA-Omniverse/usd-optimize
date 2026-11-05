.. AUTO GENERATED FILE - DO NOT EDIT

=================
Remove Attributes
=================

**Key**: ``removeAttributes``

This operation will find and remove attributes from prims. Exact attribute names can be specified, or namespaces can be used to remove any attribute with a matching namespace prefix.

When using the Remove mode, attributes can only be removed from the current edit target. This means that if the edit target has an opinion on an attribute, and there is also a weaker opinion (for example, overriding an attribute on a prim that has been referenced), then the original referenced value will now be in use. If there was no stronger opinion in the current edit target (for example, a prim was referenced and does not override the attribute), then nothing happens, as the edit target contains no opinion to remove.

Blocking an attribute means the attribute remains authored but has no value. This becomes the strongest opinion meaning regardless of the prims composition the attribute has no effective value.

Arguments
---------

Prim Paths
^^^^^^^^^^

A list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Mode
^^^^

What to do with matching attributes

- Name: ``mode``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: Remove``
  - ``1: Block``


Attributes
^^^^^^^^^^

A list of attributes or namespaces to remove

- Name: ``attributes``
- Type: ``[string]``
- Default Value: ``[]``

