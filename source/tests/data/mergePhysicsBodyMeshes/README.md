# Merge Physics Body Meshes examples

Open a USDA file in a USD viewer to inspect its input geometry and physics schemas.
Several examples share each file. Set the operation's `paths` to the example you
want to try, and use the options shown in its Python test. For example, the
separate-body discovery case uses `discovery.usda` with:

```json
{"paths": ["/Robot", "/Standalone"]}
```

Some examples select a child path to test traversal limits. Keep the fixture
files together so that relative references resolve. Save interactive results to
a separate output file.

The Python tests load these scenes and check the result. Some tests change one
property or option to cover several related cases; their setup shows the exact
variation. Numeric limits, malformed inputs, and tests that edit referenced
layers can still use anonymous stages.

Keep new examples small and name prims by their role. Group related examples in
an existing file when practical, and keep numbers and arrays easy to review.
Use `doc` metadata for useful structural descriptions and keep usage guidance here
or in the tests.
