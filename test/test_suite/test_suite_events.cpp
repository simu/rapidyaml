#include "test_suite_events.hpp"

#include "test_suite_common.hpp"
#include <c4/yml/detail/stack.hpp>
#include <gtest/gtest.h>

namespace c4 {
namespace yml {

namespace /*anon*/ {

struct OptionalScalar
{
    csubstr val = {};
    bool was_set = false;
    inline operator csubstr() const { return get(); }
    inline operator bool() const { return was_set; }
    void operator= (csubstr v) { val = v; was_set = true; }
    csubstr get() const { RYML_ASSERT(was_set); return val; }
};

size_t to_chars(c4::substr buf, OptionalScalar const& s)
{
    if(!s)
        return 0u;
    if(s.val.len <= buf.len)
        memcpy(buf.str, s.val.str, s.val.len);
    return s.val.len;
}

csubstr filtered_scalar(csubstr orig, Tree *tree)
{
    csubstr str = orig;
    csubstr tokens[] = {"\\n", "\\t", "\\\n", "\\\t"};
    csubstr::first_of_any_result result = str.first_of_any_iter(std::begin(tokens), std::end(tokens));
    if(!result)
        return str;
    substr buf = tree->alloc_arena(str.len);
    size_t bufpos = 0;
    size_t strpos = 0;
    do
    {
        // copy up to result
        _nfo_logf("result.pos={}  strpos={}", result.pos, strpos);
        RYML_CHECK(bufpos + result.pos <= buf.len);
        RYML_CHECK(result.pos >= strpos);
        memcpy(buf.str + bufpos, str.str + strpos, result.pos - strpos);
        bufpos += result.pos - strpos;
        strpos += result.pos - strpos;
        // now transform or copy the token
        switch(result.which)
        {
        case 0u:
            RYML_ASSERT(str.sub(result.pos).begins_with("\\n"));
            memcpy(buf.str + bufpos, "\n", 1u);
            bufpos += 1u;
            strpos += 2u;
            break;
        case 1u:
            RYML_ASSERT(str.sub(result.pos).begins_with("\\t"));
            memcpy(buf.str + bufpos, "\t", 1u);
            bufpos += 1u;
            strpos += 2u;
            break;
        case 2u:
            RYML_ASSERT(str.sub(result.pos).begins_with("\\\n"));
            memcpy(buf.str + bufpos, "\\n", 2u);
            bufpos += 2u;
            strpos += 3u;
            break;
        case 3u:
            RYML_ASSERT(str.sub(result.pos).begins_with("\\\t"));
            memcpy(buf.str + bufpos, "\\t", 2u);
            bufpos += 2u;
            strpos += 3u;
            break;
        default:
            C4_ERROR("never reach this");
            break;
        }
        str = str.sub(strpos);
        strpos = 0u;
        result = str.first_of_any_iter(std::begin(tokens), std::end(tokens));
    } while(result);
    // and finally copy the remaining tail portion
    memcpy(buf.str + bufpos, str.str + strpos, str.len - strpos);
    bufpos += str.len - strpos;
    strpos += str.len - strpos;
    RYML_CHECK(bufpos <= buf.len);
    RYML_CHECK(strpos == str.len);
    _nfo_logf("filtered scalar '{}' ---> '{}'", orig, buf.first(bufpos));
    return buf.first(bufpos);
}


struct Scalar
{
    OptionalScalar scalar = {};
    OptionalScalar anchor = {};
    OptionalScalar ref    = {};
    OptionalScalar tag    = {};
    bool           quoted = false;
    inline operator bool() const { if(anchor || tag) { RYML_ASSERT(scalar); } return scalar.was_set; }
    void add_key_props(Tree *tree, size_t node) const
    {
        if(ref)
        {
            _nfo_logf("node[{}]: set key ref: '{}'", node, ref);
            tree->set_key_ref(node, ref);
        }
        if(anchor)
        {
            _nfo_logf("node[{}]: set key anchor: '{}'", node, anchor);
            tree->set_key_anchor(node, anchor);
        }
        if(tag)
        {
            csubstr ntag = normalize_tag(tag);
            _nfo_logf("node[{}]: set key tag: '{}' -> '{}'", node, tag, ntag);
            tree->set_key_tag(node, ntag);
        }
        if(quoted)
        {
            _nfo_logf("node[{}]: set key as quoted", node);
            tree->_add_flags(node, KEYQUO);
        }
    }
    void add_val_props(Tree *tree, size_t node) const
    {
        if(ref)
        {
            _nfo_logf("node[{}]: set val ref: '{}'", node, ref);
            tree->set_val_ref(node, ref);
        }
        if(anchor)
        {
            _nfo_logf("node[{}]: set val anchor: '{}'", node, anchor);
            tree->set_val_anchor(node, anchor);
        }
        if(tag)
        {
            csubstr ntag = normalize_tag(tag);
            _nfo_logf("node[{}]: set val tag: '{}' -> '{}'", node, tag, ntag);
            tree->set_val_tag(node, ntag);
        }
        if(quoted)
        {
            _nfo_logf("node[{}]: set val as quoted", node);
            tree->_add_flags(node, VALQUO);
        }
    }
    csubstr filtered_scalar(Tree *tree) const
    {
        return ::c4::yml::filtered_scalar(scalar, tree);
    }
};

csubstr parse_anchor_and_tag(csubstr tokens, OptionalScalar *anchor, OptionalScalar *tag)
{
    *anchor = OptionalScalar{};
    *tag = OptionalScalar{};
    if(tokens.begins_with('&'))
    {
        size_t pos = tokens.first_of(' ');
        if(pos == (size_t)csubstr::npos)
        {
            *anchor = tokens.sub(1);
            tokens = {};
        }
        else
        {
            *anchor = tokens.first(pos).sub(1);
            tokens = tokens.right_of(pos);
        }
        _nfo_logf("anchor: {}", *anchor);
    }
    if(tokens.begins_with('<'))
    {
        size_t pos = tokens.find('>');
        RYML_ASSERT(pos != (size_t)csubstr::npos);
        *tag = tokens.first(pos + 1);
        tokens = tokens.right_of(pos).triml(' ');
        _nfo_logf("tag: {}", *tag);
    }
    return tokens;
}

} // namespace /*anon*/

void EventsParser::parse(csubstr src, Tree *C4_RESTRICT tree_)
{
    struct ParseLevel { size_t tree_node; };
    detail::stack<ParseLevel> m_stack = {};
    Tree &C4_RESTRICT tree = *tree_;
    size_t linenum = 0;
    Scalar key = {};
    _nfo_logf("parsing events! src:\n{}", src);
    for(csubstr line : src.split('\n'))
    {
        line = line.trimr('\r');
        _nfo_printf("\n\n-----------------------\n");
        {
            size_t curr = m_stack.empty() ? tree.root_id() : m_stack.top().tree_node;
            _nfo_llog("");
            _nfo_logf("line[{}]: top={} type={}", linenum, curr, NodeType::type_str(tree.type(curr)));
        }
        if(line.begins_with("=VAL "))
        {
            line = line.stripl("=VAL ");
            ASSERT_GE(m_stack.size(), 0u);
            Scalar curr = {};
            line = parse_anchor_and_tag(line, &curr.anchor, &curr.tag);
            if(line.begins_with('"') || line.begins_with('\''))
            {
                _nfo_llog("quoted scalar!");
                curr.scalar = line.sub(1);
                curr.quoted = true;
            }
            else if(line.begins_with('|') || line.begins_with('>'))
            {
                curr.scalar = line.sub(1);
                _nfo_llog("block scalar!");
            }
            else
            {
                ASSERT_TRUE(line.begins_with(':'));
                curr.scalar = line.sub(1);
            }
            _nfo_logf("parsed scalar: '{}'", curr.scalar);
            if(m_stack.empty())
            {
                _nfo_log("stack was empty, pushing root as DOC...");
                //tree._p(tree.root_id())->m_type.add(DOC);
                m_stack.push({tree.root_id()});
            }
            ParseLevel &top = m_stack.top();
            if(tree.is_seq(top.tree_node))
            {
                _nfo_logf("is seq! seq_id={}", top.tree_node);
                ASSERT_FALSE(key);
                ASSERT_TRUE(curr);
                _nfo_logf("seq[{}]: adding child", top.tree_node);
                size_t node = tree.append_child(top.tree_node);
                NodeType_e as_doc = tree.is_stream(top.tree_node) ? DOC : NOTYPE;
                _nfo_logf("seq[{}]: child={} val='{}' as_doc=", top.tree_node, node, curr.scalar, NodeType::type_str(as_doc));
                tree.to_val(node, curr.filtered_scalar(&tree), as_doc);
                curr.add_val_props(&tree, node);
            }
            else if(tree.is_map(top.tree_node))
            {
                _nfo_logf("is map! map_id={}", top.tree_node);
                if(!key)
                {
                    _nfo_logf("store key='{}' anchor='{}' tag='{}' quoted={}", curr.scalar, curr.anchor, curr.tag, curr.quoted);
                    key = curr;
                }
                else
                {
                    _nfo_logf("map[{}]: adding child", top.tree_node);
                    size_t node = tree.append_child(top.tree_node);
                    NodeType_e as_doc = tree.is_stream(top.tree_node) ? DOC : NOTYPE;
                    _nfo_logf("map[{}]: child={} key='{}' val='{}' as_doc={}", top.tree_node, node, key.scalar, curr.scalar, NodeType::type_str(as_doc));
                    tree.to_keyval(node, key.filtered_scalar(&tree), curr.filtered_scalar(&tree), as_doc);
                    key.add_key_props(&tree, node);
                    curr.add_val_props(&tree, node);
                    _nfo_logf("clear key='{}'", key.scalar);
                    key = {};
                }
            }
            else
            {
                _nfo_logf("setting tree_node={} to DOCVAL...", top.tree_node);
                tree.to_val(top.tree_node, curr.filtered_scalar(&tree), DOC);
                curr.add_val_props(&tree, top.tree_node);
            }
        }
        else if(line.begins_with("=ALI "))
        {
            csubstr alias = line.stripl("=ALI ");
            _nfo_logf("REF token: {}", alias);
            ParseLevel top = m_stack.top();
            if(tree.is_seq(top.tree_node))
            {
                _nfo_logf("node[{}] is seq: set {} as val ref", top.tree_node, alias);
                ASSERT_FALSE(key);
                size_t node = tree.append_child(top.tree_node);
                tree.to_val(node, alias);
                tree.set_val_ref(node, alias);
            }
            else if(tree.is_map(top.tree_node))
            {
                if(key)
                {
                    _nfo_logf("node[{}] is map and key '{}' is pending: set {} as val ref", top.tree_node, key.scalar, alias);
                    size_t node = tree.append_child(top.tree_node);
                    tree.to_keyval(node, key.filtered_scalar(&tree), alias);
                    key.add_key_props(&tree, node);
                    tree.set_val_ref(node, alias);
                    _nfo_logf("clear key='{}'", key);
                    key = {};
                }
                else
                {
                    _nfo_logf("node[{}] is map and no key is pending: save {} as key ref", top.tree_node, alias);
                    key.scalar = alias;
                    key.ref = alias;
                }
            }
            else
            {
                C4_ERROR("ALI event requires map or seq");
            }
        }
        else if(line.begins_with("+SEQ"))
        {
            _nfo_log("pushing SEQ");
            OptionalScalar anchor = {};
            OptionalScalar tag = {};
            parse_anchor_and_tag(line.stripl("+SEQ").triml(' '), &anchor, &tag);
            size_t node = tree.root_id();
            if(m_stack.empty())
            {
                _nfo_log("stack was empty, set root to SEQ");
                tree._add_flags(node, SEQ);
                m_stack.push({node});
                ASSERT_FALSE(key); // for the key to exist, the parent must exist and be a map
            }
            else
            {
                size_t parent = m_stack.top().tree_node;
                _nfo_logf("stack was not empty. parent={}", parent);
                ASSERT_NE(parent, (size_t)NONE);
                NodeType more_flags = NOTYPE;
                if(tree.is_doc(parent) && !(tree.is_seq(parent) || tree.is_map(parent)))
                {
                    _nfo_logf("set node to parent={}, add DOC", parent);
                    node = parent;
                    more_flags.add(DOC);
                }
                else
                {
                    _nfo_logf("add child to parent={}", parent);
                    node = tree.append_child(parent);
                    m_stack.push({node});
                    _nfo_logf("add child to parent={}: child={}", parent, node);
                }
                if(key)
                {
                    _nfo_logf("has key, set to keyseq: parent={} child={} key='{}'", parent, node, key);
                    ASSERT_EQ(tree.is_map(parent) || node == parent, true);
                    tree.to_seq(node, key.filtered_scalar(&tree), more_flags);
                    key.add_key_props(&tree, node);
                    _nfo_logf("clear key='{}'", key.scalar);
                    key = {};
                }
                else
                {
                    if(tree.is_map(parent))
                    {
                        _nfo_logf("has null key, set to keyseq: parent={} child={}", parent, node);
                        ASSERT_EQ(tree.is_map(parent) || node == parent, true);
                        tree.to_seq(node, csubstr{}, more_flags);
                    }
                    else
                    {
                        _nfo_logf("no key, set to seq: parent={} child={}", parent, node);
                        tree.to_seq(node, more_flags);
                    }
                }
            }
            if(tag)
                tree.set_val_tag(node, normalize_tag(tag));
            if(anchor)
                tree.set_val_anchor(node, anchor);
        }
        else if(line.begins_with("+MAP"))
        {
            _nfo_log("pushing MAP");
            OptionalScalar anchor = {};
            OptionalScalar tag = {};
            parse_anchor_and_tag(line.stripl("+MAP").triml(' '), &anchor, &tag);
            size_t node = tree.root_id();
            if(m_stack.empty())
            {
                _nfo_log("stack was empty, set root to MAP");
                tree._add_flags(node, MAP);
                m_stack.push({node});
                ASSERT_FALSE(key); // for the key to exist, the parent must exist and be a map
            }
            else
            {
                size_t parent = m_stack.top().tree_node;
                _nfo_logf("stack was not empty. parent={}", parent);
                ASSERT_NE(parent, (size_t)NONE);
                NodeType more_flags = NOTYPE;
                if(tree.is_doc(parent) && !(tree.is_seq(parent) || tree.is_map(parent)))
                {
                    _nfo_logf("set node to parent={}, add DOC", parent);
                    node = parent;
                    more_flags.add(DOC);
                }
                else
                {
                    _nfo_logf("add child to parent={}", parent);
                    node = tree.append_child(parent);
                    m_stack.push({node});
                    _nfo_logf("add child to parent={}: child={}", parent, node);
                }
                if(key)
                {
                    _nfo_logf("has key, set to keymap: parent={} child={} key='{}'", parent, node, key);
                    ASSERT_EQ(tree.is_map(parent) || node == parent, true);
                    tree.to_map(node, key.filtered_scalar(&tree), more_flags);
                    key.add_key_props(&tree, node);
                    _nfo_logf("clear key='{}'", key.scalar);
                    key = {};
                }
                else
                {
                    if(tree.is_map(parent))
                    {
                        _nfo_logf("has null key, set to keymap: parent={} child={}", parent, node);
                        ASSERT_EQ(tree.is_map(parent) || node == parent, true);
                        tree.to_map(node, csubstr{}, more_flags);
                    }
                    else
                    {
                        _nfo_logf("no key, set to map: parent={} child={}", parent, node);
                        tree.to_map(node, more_flags);
                    }
                }
            }
            if(tag)
                tree.set_val_tag(node, normalize_tag(tag));
            if(anchor)
                tree.set_val_anchor(node, anchor);
        }
        else if(line.begins_with("-SEQ"))
        {
            _nfo_logf("popping SEQ, empty={}", m_stack.empty());
            size_t node;
            if(m_stack.empty())
                node = tree.root_id();
            else
                node = m_stack.pop().tree_node;
            ASSERT_TRUE(tree.is_seq(node)) << "node=" << node;
        }
        else if(line.begins_with("-MAP"))
        {
            _nfo_logf("popping MAP, empty={}", m_stack.empty());
            size_t node;
            if(m_stack.empty())
                node = tree.root_id();
            else
                node = m_stack.pop().tree_node;
            ASSERT_TRUE(tree.is_map(node)) << "node=" << node;
        }
        else if(line.begins_with("+DOC"))
        {
            csubstr rem = line.stripl("+DOC").triml(' ');
            _nfo_logf("pushing DOC: {}", rem);
            size_t node = tree.root_id();
            auto is_sep = rem.first_of_any("---\n", "--- ", "---\r") || rem.ends_with("---");
            ASSERT_EQ(key, false); // for the key to exist, the parent must exist and be a map
            if(m_stack.empty())
            {
                _nfo_log("stack was empty");
                ASSERT_EQ(node, tree.root_id());
                if(tree.is_stream(node))
                {
                    _nfo_log("there is already a stream, append a DOC");
                    node = tree.append_child(node);
                    tree.to_doc(node);
                    m_stack.push({node});
                }
                else if(is_sep)
                {
                    _nfo_logf("separator was specified: {}", rem);
                    if((!tree.is_container(node)) && (!tree.is_doc(node)))
                    {
                        tree._add_flags(node, STREAM);
                        node = tree.append_child(node);
                        _nfo_logf("create STREAM at {} and add DOC child={}", tree.root_id(), node);
                        tree.to_doc(node);
                        m_stack.push({node});
                    }
                    else
                    {
                        _nfo_log("rearrange root as STREAM");
                        tree.set_root_as_stream();
                        node = tree.append_child(tree.root_id());
                        _nfo_logf("added doc as STREAM child: {}", node);
                        tree.to_doc(node);
                        m_stack.push({node});
                    }
                }
                else
                {
                    if(tree.is_doc(node))
                    {
                        _nfo_log("rearrange root as STREAM");
                        tree.set_root_as_stream();
                        m_stack.push({node});
                    }
                }
            }
            else
            {
                size_t parent = m_stack.top().tree_node;
                _nfo_logf("add DOC to parent={}", parent);
                ASSERT_NE(parent, (size_t)NONE);
                node = tree.append_child(parent);
                _nfo_logf("child DOC={}", node);
                tree.to_doc(node);
                m_stack.push({node});
            }
        }
        else if(line.begins_with("-DOC"))
        {
            _nfo_log("popping DOC");
            if(!m_stack.empty())
                m_stack.pop();
        }
        else if(line.begins_with("+STR"))
        {
            ASSERT_EQ(m_stack.size(), 0u);
        }
        else if(line.begins_with("-STR"))
        {
            ASSERT_LE(m_stack.size(), 1u);
            if(!m_stack.empty())
                m_stack.pop();
        }
        else if(line.empty())
        {
            // nothing to do
        }
        else
        {
            C4_ERROR("unknown event: '%.*s'", (int)line.len, line.str);
        }
        linenum++;
    }
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

TEST(EventsParser, parse_tags)
{
    {
        OptionalScalar anchor = {};
        OptionalScalar tag = {};
        csubstr tokens = parse_anchor_and_tag("<tag:yaml.org,2002:int> :2", &anchor, &tag);
        EXPECT_EQ(tokens, ":2");
        EXPECT_TRUE(tag);
        EXPECT_EQ(tag.val, "<tag:yaml.org,2002:int>");
        EXPECT_FALSE(anchor);
    }
    {
        OptionalScalar anchor = {};
        OptionalScalar tag = {};
        csubstr tokens = parse_anchor_and_tag("<!foo> \"bar", &anchor, &tag);
        EXPECT_EQ(tokens, "\"bar");
        EXPECT_TRUE(tag);
        EXPECT_EQ(tag.val, "<!foo>");
        EXPECT_FALSE(anchor);
    }
}

} // namespace yml
} // namespace c4
