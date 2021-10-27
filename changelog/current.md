
### New features
- Add support for CPU architectures aarch64, ppc64le, s390x.
- Update c4core to [0.1.6](https://github.com/biojppm/c4core/releases/tag/v0.1.6)
- `Tree` and `NodeRef`: add document getter `doc()` and `docref()`
  ```c++
  Tree tree = parse(R"(---
  doc0
  ---
  doc1
  )");
  NodeRef stream = t.rootref();
  assert(stream.is_stream());
  // tree.doc(i): get the index of the i-th doc node.
  // Equivalent to tree.child(tree.root_id(), i)
  assert(tree.doc(0) == 1u);
  assert(tree.doc(1) == 2u);
  // tree.docref(i), same as above, return NodeRef
  assert(tree.docref(0).val() == "doc0");
  assert(tree.docref(1).val() == "doc1");
  // stream.doc(i), same as above, given NodeRef
  assert(stream.doc(0).val() == "doc0");
  assert(stream.doc(1).val() == "doc1");
  ```

### Fixes

- Fix compilation with `C4CORE_NO_FAST_FLOAT` ([PR #163](https://github.com/biojppm/rapidyaml/pulls/163))

#### Tags

- Fix parsing of tags followed by comments in sequences ([PR #161](https://github.com/biojppm/rapidyaml/pull/161)):
  ```yaml
  # test case 735Y
  - !!map # Block collection
    foo : bar
  ```

#### Block scalars
- Fix parsing of block spec with both chomping and indentation: chomping may come before or after the indentation:
  ```yaml
  # the block scalar specs below now have the same effect.
  # test cases: D83L, P2AD
  - |2-
    explicit indent and chomp
  - |-2
    chomp and explicit indent
  ```
- Fix [inference of block indentation](https://yaml.org/spec/1.2.2/#8111-block-indentation-indicator) with leading blank lines ([PR #161](https://github.com/biojppm/rapidyaml/pull/161)):
  ```yaml
  # test cases: 4QFQ, 7T8X
  - >
   
    
    # child1
  # parsed as "\n\n child1"
  --- # test case DWX9
  |
   
    
    literal
     
    
    text
  
   # Comment
  # parsed as "\n\nliteral\n \n\ntext\n"
  ```
- Fix parsing of same-indentation block scalars ([PR #161](https://github.com/biojppm/rapidyaml/pull/161)):
  ```yaml
  # test case W4TN
  # all docs have the same value: "%!PS-Adobe-2.0"
  --- |
   %!PS-Adobe-2.0
  ...
  --- >
   %!PS-Adobe-2.0
  ...
  --- |
  %!PS-Adobe-2.0
  ...
  --- >
  %!PS-Adobe-2.0
  ...
  --- |
   %!PS-Adobe-2.0
  --- >
   %!PS-Adobe-2.0
  --- |
  %!PS-Adobe-2.0
  --- >
  %!PS-Adobe-2.0
  ```

#### Plain scalars
- Fix parsing of whitespace within plain scalars ([PR #161](https://github.com/biojppm/rapidyaml/pull/161)):
  ```yaml
  ---
  # test case NB6Z
  key:
    value
    with
     	
    tabs
    tabs
     	
      foo
     	
        bar
          baz
     	
  # is now correctly parsed as "value with\ntabs tabs\nfoo\nbar baz"
  ---
  # test case 9YRD, EX5H (trailing whitespace)
  a
  b  
    c
  d
  
  e
  # is now correctly parsed as "a b c d\ne"
  ```
- Fix parsing of unindented plain scalars at the root level scope ([PR #161](https://github.com/biojppm/rapidyaml/pull/161))
  ```yaml
  --- # this parsed
  Bare
   scalar
   is indented
  # was correctly parsed as "Bare scalar is indented"
  --- # but this failed to parse successfully:
  Bare
  scalar
  is not indented
  # is now correctly parsed as "Bare scalar is not indented"
  --- # test case NB6Z
  value
  with
   	
  tabs
  tabs
   	
    foo
   	
      bar
        baz
    	
  # now correctly parsed as "value with\ntabs tabs\nfoo\nbar baz"
  ---
  --- # test cases EXG3, 82AN
  ---word1
  word2
  # now correctly parsed as "---word1 word2"
  ```
- Fix parsing of comments within plain scalars
  ```yaml
  # test case 7TMG
  --- # now correctly parsed as "word1"
  word1
  # comment
  --- # now correctly parsed as [word1, word2]
  [ word1
  # comment
  , word2]
  ```

### Thanks

--- @mbs-c
