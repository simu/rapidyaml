#include "c4/yml/tree.hpp"
#include "c4/yml/detail/parser_dbg.hpp"
#include "c4/yml/node.hpp"
#include "c4/yml/detail/stack.hpp"


C4_SUPPRESS_WARNING_GCC_WITH_PUSH("-Wtype-limits")
C4_SUPPRESS_WARNING_MSVC_WITH_PUSH(4296/*expression is always 'boolean_value'*/)


namespace c4 {
namespace yml {

csubstr normalize_tag(csubstr tag)
{
    YamlTag_e t = to_tag(tag);
    if(t != TAG_NONE)
        return from_tag(t);
    if(tag.begins_with("!<"))
        tag = tag.sub(1);
    if(tag.begins_with("<!"))
    {
        size_t pos = tag.find('>');
        if(pos == csubstr::npos)
            return tag;
        return tag.range(1, pos);
    }
    return tag;
}

YamlTag_e to_tag(csubstr tag)
{
    if(tag.begins_with("!<"))
        tag = tag.sub(1);
    if(tag.begins_with("!!"))
        tag = tag.sub(2);
    else if(tag.begins_with('!'))
        return TAG_NONE;
    else if(tag.begins_with("tag:yaml.org,2002:"))
    {
        RYML_ASSERT(csubstr("tag:yaml.org,2002:").len == 18);
        tag = tag.sub(18);
    }
    else if(tag.begins_with("<tag:yaml.org,2002:"))
    {
        RYML_ASSERT(csubstr("<tag:yaml.org,2002:").len == 19);
        tag = tag.sub(19);
        if(!tag.len)
            return TAG_NONE;
        tag = tag.offs(0, 1);
    }

    if(tag == "map")
        return TAG_MAP;
    else if(tag == "omap")
        return TAG_OMAP;
    else if(tag == "pairs")
        return TAG_PAIRS;
    else if(tag == "set")
        return TAG_SET;
    else if(tag == "seq")
        return TAG_SEQ;
    else if(tag == "binary")
        return TAG_BINARY;
    else if(tag == "bool")
        return TAG_BOOL;
    else if(tag == "float")
        return TAG_FLOAT;
    else if(tag == "int")
        return TAG_INT;
    else if(tag == "merge")
        return TAG_MERGE;
    else if(tag == "null")
        return TAG_NULL;
    else if(tag == "str")
        return TAG_STR;
    else if(tag == "timestamp")
        return TAG_TIMESTAMP;
    else if(tag == "value")
        return TAG_VALUE;

    return TAG_NONE;
}

csubstr from_tag(YamlTag_e tag)
{
    switch(tag)
    {
    case TAG_MAP:
        return {"!!map"};
    case TAG_OMAP:
        return {"!!omap"};
    case TAG_PAIRS:
        return {"!!pairs"};
    case TAG_SET:
        return {"!!set"};
    case TAG_SEQ:
        return {"!!seq"};
    case TAG_BINARY:
        return {"!!binary"};
    case TAG_BOOL:
        return {"!!bool"};
    case TAG_FLOAT:
        return {"!!float"};
    case TAG_INT:
        return {"!!int"};
    case TAG_MERGE:
        return {"!!merge"};
    case TAG_NULL:
        return {"!!null"};
    case TAG_STR:
        return {"!!str"};
    case TAG_TIMESTAMP:
        return {"!!timestamp"};
    case TAG_VALUE:
        return {"!!value"};
    case TAG_YAML:
        return {"!!yaml"};
    case TAG_NONE:
        return {""};
    }
    return {""};
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

const char* NodeType::type_str(NodeType_e ty)
{
    switch(ty & _TYMASK)
    {
    case KEYVAL:
        return "KEYVAL";
    case KEY:
        return "KEY";
    case VAL:
        return "VAL";
    case MAP:
        return "MAP";
    case SEQ:
        return "SEQ";
    case KEYMAP:
        return "KEYMAP";
    case KEYSEQ:
        return "KEYSEQ";
    case DOCSEQ:
        return "DOCSEQ";
    case DOCMAP:
        return "DOCMAP";
    case DOCVAL:
        return "DOCVAL";
    case DOC:
        return "DOC";
    case STREAM:
        return "STREAM";
    case NOTYPE:
        return "NOTYPE";
    default:
        if((ty & KEYVAL) == KEYVAL)
            return "KEYVAL***";
        if((ty & KEYMAP) == KEYMAP)
            return "KEYMAP***";
        if((ty & KEYSEQ) == KEYSEQ)
            return "KEYSEQ***";
        if((ty & DOCSEQ) == DOCSEQ)
            return "DOCSEQ***";
        if((ty & DOCMAP) == DOCMAP)
            return "DOCMAP***";
        if((ty & DOCVAL) == DOCVAL)
            return "DOCVAL***";
        if(ty & KEY)
            return "KEY***";
        if(ty & VAL)
            return "VAL***";
        if(ty & MAP)
            return "MAP***";
        if(ty & SEQ)
            return "SEQ***";
        if(ty & DOC)
            return "DOC***";
        return "(unk)";
    }
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

NodeRef Tree::rootref()
{
    return NodeRef(this, root_id());
}
NodeRef const Tree::rootref() const
{
    return NodeRef(const_cast<Tree*>(this), root_id());
}

NodeRef Tree::ref(size_t id)
{
    RYML_ASSERT(id != NONE && id >= 0 && id < m_size);
    return NodeRef(this, id);
}
NodeRef const Tree::ref(size_t id) const
{
    RYML_ASSERT(id != NONE && id >= 0 && id < m_size);
    return NodeRef(const_cast<Tree*>(this), id);
}

NodeRef Tree::operator[] (csubstr key)
{
    return rootref()[key];
}
NodeRef const Tree::operator[] (csubstr key) const
{
    return rootref()[key];
}

NodeRef Tree::operator[] (size_t i)
{
    return rootref()[i];
}
NodeRef const Tree::operator[] (size_t i) const
{
    return rootref()[i];
}


//-----------------------------------------------------------------------------
Tree::Tree(Allocator const& cb)
:
    m_buf(nullptr),
    m_cap(0),
    m_size(0),
    m_free_head(NONE),
    m_free_tail(NONE),
    m_arena(),
    m_arena_pos(0),
    m_alloc(cb)
{
}

Tree::Tree(size_t node_capacity, size_t arena_capacity, Allocator const& cb) : Tree(cb)
{
    reserve(node_capacity);
    reserve_arena(arena_capacity);
}

Tree::~Tree()
{
    _free();
}


Tree::Tree(Tree const& that) noexcept : Tree(that.m_alloc)
{
    _copy(that);
}

Tree& Tree::operator= (Tree const& that) noexcept
{
    _free();
    _copy(that);
    return *this;
}

Tree::Tree(Tree && that) noexcept : Tree(that.m_alloc)
{
    _move(that);
}

Tree& Tree::operator= (Tree && that) noexcept
{
    _free();
    _move(that);
    return *this;
}

void Tree::_free()
{
    if(m_buf)
    {
        RYML_ASSERT(m_cap > 0);
        m_alloc.free(m_buf, m_cap * sizeof(NodeData));
    }
    if(m_arena.str)
    {
        RYML_ASSERT(m_arena.len > 0);
        m_alloc.free(m_arena.str, m_arena.len);
    }
    _clear();
}


C4_SUPPRESS_WARNING_GCC_PUSH
#if defined(__GNUC__) && __GNUC__>= 8
    C4_SUPPRESS_WARNING_GCC_WITH_PUSH("-Wclass-memaccess") // error: ‘void* memset(void*, int, size_t)’ clearing an object of type ‘class c4::yml::Tree’ with no trivial copy-assignment; use assignment or value-initialization instead
#endif

void Tree::_clear()
{
    m_buf = nullptr;
    m_cap = 0;
    m_size = 0;
    m_free_head = 0;
    m_free_tail = 0;
    m_arena = {};
    m_arena_pos = 0;
}

void Tree::_copy(Tree const& that)
{
    RYML_ASSERT(m_buf == nullptr);
    RYML_ASSERT(m_arena.str == nullptr);
    RYML_ASSERT(m_arena.len == 0);
    m_buf = (NodeData*) m_alloc.allocate(that.m_cap * sizeof(NodeData), that.m_buf);
    memcpy(m_buf, that.m_buf, that.m_cap * sizeof(NodeData));
    m_cap = that.m_cap;
    m_size = that.m_size;
    m_free_head = that.m_free_head;
    m_free_tail = that.m_free_tail;
    m_arena_pos = that.m_arena_pos;
    m_arena = that.m_arena;
    if(that.m_arena.str)
    {
        RYML_ASSERT(that.m_arena.len > 0);
        substr arena;
        arena.str = (char*) m_alloc.allocate(that.m_arena.len, that.m_arena.str);
        arena.len = that.m_arena.len;
        _relocate(arena); // does a memcpy of the arena and updates nodes using the old arena
        m_arena = arena;
    }
}

void Tree::_move(Tree & that)
{
    RYML_ASSERT(m_buf == nullptr);
    RYML_ASSERT(m_arena.str == nullptr);
    RYML_ASSERT(m_arena.len == 0);
    m_buf = that.m_buf;
    m_cap = that.m_cap;
    m_size = that.m_size;
    m_free_head = that.m_free_head;
    m_free_tail = that.m_free_tail;
    m_arena = that.m_arena;
    m_arena_pos = that.m_arena_pos;
    that._clear();
}

void Tree::_relocate(substr next_arena)
{
    RYML_ASSERT(next_arena.not_empty());
    RYML_ASSERT(next_arena.len >= m_arena.len);
    memcpy(next_arena.str, m_arena.str, m_arena_pos);
    for(NodeData *C4_RESTRICT n = m_buf, *e = m_buf + m_cap; n != e; ++n)
    {
        if(in_arena(n->m_key.scalar))
            n->m_key.scalar = _relocated(n->m_key.scalar, next_arena);
        if(in_arena(n->m_key.tag   ))
            n->m_key.tag    = _relocated(n->m_key.tag   , next_arena);
        if(in_arena(n->m_key.anchor))
            n->m_key.anchor = _relocated(n->m_key.anchor, next_arena);
        if(in_arena(n->m_val.scalar))
            n->m_val.scalar = _relocated(n->m_val.scalar, next_arena);
        if(in_arena(n->m_val.tag   ))
            n->m_val.tag    = _relocated(n->m_val.tag   , next_arena);
        if(in_arena(n->m_val.anchor))
            n->m_val.anchor = _relocated(n->m_val.anchor, next_arena);
    }
}


//-----------------------------------------------------------------------------
void Tree::reserve(size_t cap)
{
    if(cap > m_cap)
    {
        NodeData *buf = (NodeData*) m_alloc.allocate(cap * sizeof(NodeData), m_buf);
        if(m_buf)
        {
            memcpy(buf, m_buf, m_cap * sizeof(NodeData));
            m_alloc.free(m_buf, m_cap * sizeof(NodeData));
        }
        size_t first = m_cap, del = cap - m_cap;
        m_cap = cap;
        m_buf = buf;
        _clear_range(first, del);

        if(m_free_head != NONE)
        {
            RYML_ASSERT(m_buf != nullptr);
            RYML_ASSERT(m_free_tail != NONE);
            m_buf[m_free_tail].m_next_sibling = first;
            m_buf[first].m_prev_sibling = m_free_tail;
            m_free_tail = cap-1;
        }
        else
        {
            RYML_ASSERT(m_free_tail == NONE);
            m_free_head = first;
            m_free_tail = cap-1;
        }
        RYML_ASSERT(m_free_head == NONE || (m_free_head >= 0 && m_free_head < cap));
        RYML_ASSERT(m_free_tail == NONE || (m_free_tail >= 0 && m_free_tail < cap));

        if( ! m_size)
            _claim_root();
    }
}


//-----------------------------------------------------------------------------
void Tree::clear()
{
    _clear_range(0, m_cap);
    m_size = 0;
    if(m_buf)
    {
        RYML_ASSERT(m_cap >= 0);
        m_free_head = 0;
        m_free_tail = m_cap-1;
        _claim_root();
    }
    else
    {
        m_free_head = NONE;
        m_free_tail = NONE;
    }
}

void Tree::_claim_root()
{
    size_t r = _claim();
    RYML_ASSERT(r == 0);
    _set_hierarchy(r, NONE, NONE);
}


//-----------------------------------------------------------------------------
void Tree::_clear_range(size_t first, size_t num)
{
    if(num == 0) return; // prevent overflow when subtracting
    RYML_ASSERT(first >= 0 && first + num <= m_cap);
    memset(m_buf + first, 0, num * sizeof(NodeData));
    for(size_t i = first, e = first + num; i < e; ++i)
    {
        _clear(i);
        NodeData *n = m_buf + i;
        n->m_prev_sibling = i - 1;
        n->m_next_sibling = i + 1;
    }
    m_buf[first + num - 1].m_next_sibling = NONE;
}

C4_SUPPRESS_WARNING_GCC_POP


//-----------------------------------------------------------------------------
void Tree::_release(size_t i)
{
    RYML_ASSERT(i >= 0 && i < m_cap);

    _rem_hierarchy(i);
    _free_list_add(i);
    _clear(i);

    --m_size;
}

//-----------------------------------------------------------------------------
// add to the front of the free list
void Tree::_free_list_add(size_t i)
{
    RYML_ASSERT(i >= 0 && i < m_cap);
    NodeData &C4_RESTRICT w = m_buf[i];

    w.m_parent = NONE;
    w.m_next_sibling = m_free_head;
    w.m_prev_sibling = NONE;
    if(m_free_head != NONE)
        m_buf[m_free_head].m_prev_sibling = i;
    m_free_head = i;
    if(m_free_tail == NONE)
        m_free_tail = m_free_head;
}

void Tree::_free_list_rem(size_t i)
{
    if(m_free_head == i)
        m_free_head = _p(i)->m_next_sibling;
    _rem_hierarchy(i);
}

//-----------------------------------------------------------------------------
size_t Tree::_claim()
{
    if(m_free_head == NONE || m_buf == nullptr)
    {
        size_t sz = 2 * m_cap;
        sz = sz ? sz : 16;
        reserve(sz);
        RYML_ASSERT(m_free_head != NONE);
    }

    RYML_ASSERT(m_size < m_cap);
    RYML_ASSERT(m_free_head >= 0 && m_free_head < m_cap);

    size_t ichild = m_free_head;
    NodeData *child = m_buf + ichild;

    ++m_size;
    m_free_head = child->m_next_sibling;
    if(m_free_head == NONE)
    {
        m_free_tail = NONE;
        RYML_ASSERT(m_size == m_cap);
    }

    _clear(ichild);

    return ichild;
}

//-----------------------------------------------------------------------------

C4_SUPPRESS_WARNING_GCC_PUSH
C4_SUPPRESS_WARNING_CLANG_PUSH
C4_SUPPRESS_WARNING_CLANG("-Wnull-dereference")
#if defined(__GNUC__) && (__GNUC__ >= 6)
C4_SUPPRESS_WARNING_GCC("-Wnull-dereference")
#endif

void Tree::_set_hierarchy(size_t ichild, size_t iparent, size_t iprev_sibling)
{
    RYML_ASSERT(iparent == NONE || (iparent >= 0 && iparent < m_cap));
    RYML_ASSERT(iprev_sibling == NONE || (iprev_sibling >= 0 && iprev_sibling < m_cap));

    NodeData *C4_RESTRICT child = get(ichild);

    child->m_parent = iparent;
    child->m_prev_sibling = NONE;
    child->m_next_sibling = NONE;

    if(iparent == NONE)
    {
        RYML_ASSERT(ichild == 0);
        RYML_ASSERT(iprev_sibling == NONE);
    }

    if(iparent == NONE)
        return;

    size_t inext_sibling = iprev_sibling != NONE ? next_sibling(iprev_sibling) : first_child(iparent);
    NodeData *C4_RESTRICT parent = get(iparent);
    NodeData *C4_RESTRICT psib   = get(iprev_sibling);
    NodeData *C4_RESTRICT nsib   = get(inext_sibling);

    if(psib)
    {
        RYML_ASSERT(next_sibling(iprev_sibling) == id(nsib));
        child->m_prev_sibling = id(psib);
        psib->m_next_sibling = id(child);
        RYML_ASSERT(psib->m_prev_sibling != psib->m_next_sibling || psib->m_prev_sibling == NONE);
    }

    if(nsib)
    {
        RYML_ASSERT(prev_sibling(inext_sibling) == id(psib));
        child->m_next_sibling = id(nsib);
        nsib->m_prev_sibling = id(child);
        RYML_ASSERT(nsib->m_prev_sibling != nsib->m_next_sibling || nsib->m_prev_sibling == NONE);
    }

    if(parent->m_first_child == NONE)
    {
        RYML_ASSERT(parent->m_last_child == NONE);
        parent->m_first_child = id(child);
        parent->m_last_child = id(child);
    }
    else
    {
        if(child->m_next_sibling == parent->m_first_child)
            parent->m_first_child = id(child);

        if(child->m_prev_sibling == parent->m_last_child)
            parent->m_last_child = id(child);
    }
}

C4_SUPPRESS_WARNING_GCC_POP
C4_SUPPRESS_WARNING_CLANG_POP


//-----------------------------------------------------------------------------
void Tree::_rem_hierarchy(size_t i)
{
    RYML_ASSERT(i >= 0 && i < m_cap);

    NodeData &C4_RESTRICT w = m_buf[i];

    // remove from the parent
    if(w.m_parent != NONE)
    {
        NodeData &C4_RESTRICT p = m_buf[w.m_parent];
        if(p.m_first_child == i)
        {
            p.m_first_child = w.m_next_sibling;
        }
        if(p.m_last_child == i)
        {
            p.m_last_child = w.m_prev_sibling;
        }
    }

    // remove from the used list
    if(w.m_prev_sibling != NONE)
    {
        NodeData *C4_RESTRICT prev = get(w.m_prev_sibling);
        prev->m_next_sibling = w.m_next_sibling;
    }
    if(w.m_next_sibling != NONE)
    {
        NodeData *C4_RESTRICT next = get(w.m_next_sibling);
        next->m_prev_sibling = w.m_prev_sibling;
    }
}

//-----------------------------------------------------------------------------
void Tree::reorder()
{
    size_t r = root_id();
    _do_reorder(&r, 0);
}

//-----------------------------------------------------------------------------
size_t Tree::_do_reorder(size_t *node, size_t count)
{
    // swap this node if it's not in place
    if(*node != count)
    {
        _swap(*node, count);
        *node = count;
    }
    ++count; // bump the count from this node

    // now descend in the hierarchy
    for(size_t i = first_child(*node); i != NONE; i = next_sibling(i))
    {
        // this child may have been relocated to a different index,
        // so get an updated version
        count = _do_reorder(&i, count);
    }
    return count;
}

//-----------------------------------------------------------------------------
void Tree::_swap(size_t n_, size_t m_)
{
    RYML_ASSERT((parent(n_) != NONE) || type(n_) == NOTYPE);
    RYML_ASSERT((parent(m_) != NONE) || type(m_) == NOTYPE);
    NodeType tn = type(n_);
    NodeType tm = type(m_);
    if(tn != NOTYPE && tm != NOTYPE)
    {
        _swap_props(n_, m_);
        _swap_hierarchy(n_, m_);
    }
    else if(tn == NOTYPE && tm != NOTYPE)
    {
        _copy_props(n_, m_);
        _free_list_rem(n_);
        _copy_hierarchy(n_, m_);
        _clear(m_);
        _free_list_add(m_);
    }
    else if(tn != NOTYPE && tm == NOTYPE)
    {
        _copy_props(m_, n_);
        _free_list_rem(m_);
        _copy_hierarchy(m_, n_);
        _clear(n_);
        _free_list_add(n_);
    }
    else
    {
        C4_NEVER_REACH();
    }
}

//-----------------------------------------------------------------------------
void Tree::_swap_hierarchy(size_t ia, size_t ib)
{
    if(ia == ib) return;

    for(size_t i = first_child(ia); i != NONE; i = next_sibling(i))
    {
        if(i == ib || i == ia) continue;
        _p(i)->m_parent = ib;
    }

    for(size_t i = first_child(ib); i != NONE; i = next_sibling(i))
    {
        if(i == ib || i == ia) continue;
        _p(i)->m_parent = ia;
    }

    auto & C4_RESTRICT a  = *_p(ia);
    auto & C4_RESTRICT b  = *_p(ib);
    auto & C4_RESTRICT pa = *_p(a.m_parent);
    auto & C4_RESTRICT pb = *_p(b.m_parent);

    if(&pa == &pb)
    {
        if((pa.m_first_child == ib && pa.m_last_child == ia)
            ||
           (pa.m_first_child == ia && pa.m_last_child == ib))
        {
            std::swap(pa.m_first_child, pa.m_last_child);
        }
        else
        {
            bool changed = false;
            if(pa.m_first_child == ia) { pa.m_first_child = ib; changed = true; }
            if(pa.m_last_child  == ia) { pa.m_last_child  = ib; changed = true; }
            if(pb.m_first_child == ib && !changed) { pb.m_first_child = ia; }
            if(pb.m_last_child  == ib && !changed) { pb.m_last_child  = ia; }
        }
    }
    else
    {
        if(pa.m_first_child == ia) pa.m_first_child = ib;
        if(pa.m_last_child  == ia) pa.m_last_child  = ib;
        if(pb.m_first_child == ib) pb.m_first_child = ia;
        if(pb.m_last_child  == ib) pb.m_last_child  = ia;
    }
    std::swap(a.m_first_child , b.m_first_child);
    std::swap(a.m_last_child  , b.m_last_child);

    if(a.m_prev_sibling != ib && b.m_prev_sibling != ia &&
       a.m_next_sibling != ib && b.m_next_sibling != ia)
    {
        if(a.m_prev_sibling != NONE && a.m_prev_sibling != ib)
        {
            _p(a.m_prev_sibling)->m_next_sibling = ib;
        }
        if(a.m_next_sibling != NONE && a.m_next_sibling != ib)
        {
            _p(a.m_next_sibling)->m_prev_sibling = ib;
        }
        if(b.m_prev_sibling != NONE && b.m_prev_sibling != ia)
        {
            _p(b.m_prev_sibling)->m_next_sibling = ia;
        }
        if(b.m_next_sibling != NONE && b.m_next_sibling != ia)
        {
            _p(b.m_next_sibling)->m_prev_sibling = ia;
        }
        std::swap(a.m_prev_sibling, b.m_prev_sibling);
        std::swap(a.m_next_sibling, b.m_next_sibling);
    }
    else
    {
        if(a.m_next_sibling == ib) // n will go after m
        {
            RYML_ASSERT(b.m_prev_sibling == ia);
            if(a.m_prev_sibling != NONE)
            {
                RYML_ASSERT(a.m_prev_sibling != ib);
                _p(a.m_prev_sibling)->m_next_sibling = ib;
            }
            if(b.m_next_sibling != NONE)
            {
                RYML_ASSERT(b.m_next_sibling != ia);
                _p(b.m_next_sibling)->m_prev_sibling = ia;
            }
            size_t ns = b.m_next_sibling;
            b.m_prev_sibling = a.m_prev_sibling;
            b.m_next_sibling = ia;
            a.m_prev_sibling = ib;
            a.m_next_sibling = ns;
        }
        else if(a.m_prev_sibling == ib) // m will go after n
        {
            RYML_ASSERT(b.m_next_sibling == ia);
            if(b.m_prev_sibling != NONE)
            {
                RYML_ASSERT(b.m_prev_sibling != ia);
                _p(b.m_prev_sibling)->m_next_sibling = ia;
            }
            if(a.m_next_sibling != NONE)
            {
                RYML_ASSERT(a.m_next_sibling != ib);
                _p(a.m_next_sibling)->m_prev_sibling = ib;
            }
            size_t ns = b.m_prev_sibling;
            a.m_prev_sibling = b.m_prev_sibling;
            a.m_next_sibling = ib;
            b.m_prev_sibling = ia;
            b.m_next_sibling = ns;
        }
        else
        {
            C4_NEVER_REACH();
        }
    }
    RYML_ASSERT(a.m_next_sibling != ia);
    RYML_ASSERT(a.m_prev_sibling != ia);
    RYML_ASSERT(b.m_next_sibling != ib);
    RYML_ASSERT(b.m_prev_sibling != ib);

    if(a.m_parent != ib && b.m_parent != ia)
    {
        std::swap(a.m_parent, b.m_parent);
    }
    else
    {
        if(a.m_parent == ib && b.m_parent != ia)
        {
            a.m_parent = b.m_parent;
            b.m_parent = ia;
        }
        else if(a.m_parent != ib && b.m_parent == ia)
        {
            b.m_parent = a.m_parent;
            a.m_parent = ib;
        }
        else
        {
            C4_NEVER_REACH();
        }
    }
}

//-----------------------------------------------------------------------------
void Tree::_copy_hierarchy(size_t dst_, size_t src_)
{
    auto const& C4_RESTRICT src = *_p(src_);
    auto      & C4_RESTRICT dst = *_p(dst_);
    auto      & C4_RESTRICT prt = *_p(src.m_parent);
    for(size_t i = src.m_first_child; i != NONE; i = next_sibling(i))
    {
        _p(i)->m_parent = dst_;
    }
    if(src.m_prev_sibling != NONE)
    {
        _p(src.m_prev_sibling)->m_next_sibling = dst_;
    }
    if(src.m_next_sibling != NONE)
    {
        _p(src.m_next_sibling)->m_prev_sibling = dst_;
    }
    if(prt.m_first_child == src_)
    {
        prt.m_first_child = dst_;
    }
    if(prt.m_last_child  == src_)
    {
        prt.m_last_child  = dst_;
    }
    dst.m_parent       = src.m_parent;
    dst.m_first_child  = src.m_first_child;
    dst.m_last_child   = src.m_last_child;
    dst.m_prev_sibling = src.m_prev_sibling;
    dst.m_next_sibling = src.m_next_sibling;
}

//-----------------------------------------------------------------------------
void Tree::_swap_props(size_t n_, size_t m_)
{
    NodeData &C4_RESTRICT n = *_p(n_);
    NodeData &C4_RESTRICT m = *_p(m_);
    std::swap(n.m_type, m.m_type);
    std::swap(n.m_key, m.m_key);
    std::swap(n.m_val, m.m_val);
}

//-----------------------------------------------------------------------------
void Tree::move(size_t node, size_t after)
{
    RYML_ASSERT(node != NONE);
    RYML_ASSERT( ! is_root(node));
    RYML_ASSERT(has_sibling(node, after) && has_sibling(after, node));

    _rem_hierarchy(node);
    _set_hierarchy(node, parent(node), after);
}

//-----------------------------------------------------------------------------

void Tree::move(size_t node, size_t new_parent, size_t after)
{
    RYML_ASSERT(node != NONE);
    RYML_ASSERT(new_parent != NONE);
    RYML_ASSERT( ! is_root(node));

    _rem_hierarchy(node);
    _set_hierarchy(node, new_parent, after);
}

size_t Tree::move(Tree *src, size_t node, size_t new_parent, size_t after)
{
    RYML_ASSERT(node != NONE);
    RYML_ASSERT(new_parent != NONE);

    size_t dup = duplicate(src, node, new_parent, after);
    src->remove(node);
    return dup;
}

void Tree::set_root_as_stream()
{
    size_t root = root_id();
    if(is_stream(root))
        return;
    // don't use _add_flags() because it's checked and will fail
    if(!has_children(root))
    {
        if(is_val(root))
        {
            _p(root)->m_type.add(SEQ);
            size_t next_doc = append_child(root);
            _copy_props_wo_key(next_doc, root);
            _p(next_doc)->m_type.add(DOC);
            _p(next_doc)->m_type.rem(SEQ);
        }
        _p(root)->m_type = STREAM;
        return;
    }
    RYML_ASSERT(!has_key(root));
    size_t next_doc = append_child(root);
    _copy_props_wo_key(next_doc, root);
    _add_flags(next_doc, DOC);
    for(size_t prev = NONE, ch = first_child(root), next = next_sibling(ch); ch != NONE; )
    {
        if(ch == next_doc)
            break;
        move(ch, next_doc, prev);
        prev = ch;
        ch = next;
        next = next_sibling(next);
    }
    _p(root)->m_type = STREAM;
}


//-----------------------------------------------------------------------------
size_t Tree::duplicate(size_t node, size_t parent, size_t after)
{
    return duplicate(this, node, parent, after);
}

size_t Tree::duplicate(Tree const* src, size_t node, size_t parent, size_t after)
{
    RYML_ASSERT(src != nullptr);
    RYML_ASSERT(node != NONE);
    RYML_ASSERT(parent != NONE);
    RYML_ASSERT( ! src->is_root(node));

    size_t copy = _claim();

    _copy_props(copy, src, node);
    _set_hierarchy(copy, parent, after);
    duplicate_children(src, node, copy, NONE);

    return copy;
}

//-----------------------------------------------------------------------------
size_t Tree::duplicate_children(size_t node, size_t parent, size_t after)
{
    return duplicate_children(this, node, parent, after);
}

size_t Tree::duplicate_children(Tree const* src, size_t node, size_t parent, size_t after)
{
    RYML_ASSERT(src != nullptr);
    RYML_ASSERT(node != NONE);
    RYML_ASSERT(parent != NONE);
    RYML_ASSERT(after == NONE || has_child(parent, after));

    size_t prev = after;
    for(size_t i = src->first_child(node); i != NONE; i = src->next_sibling(i))
    {
        prev = duplicate(src, i, parent, prev);
    }

    return prev;
}

//-----------------------------------------------------------------------------
void Tree::duplicate_contents(size_t node, size_t where)
{
    duplicate_contents(this, node, where);
}

void Tree::duplicate_contents(Tree const *src, size_t node, size_t where)
{
    RYML_ASSERT(src != nullptr);
    RYML_ASSERT(node != NONE);
    RYML_ASSERT(where != NONE);
    _copy_props_wo_key(where, src, node);
    duplicate_children(src, node, where, last_child(where));
}

//-----------------------------------------------------------------------------
size_t Tree::duplicate_children_no_rep(size_t node, size_t parent, size_t after)
{
    return duplicate_children_no_rep(this, node, parent, after);
}

size_t Tree::duplicate_children_no_rep(Tree const *src, size_t node, size_t parent, size_t after)
{
    RYML_ASSERT(node != NONE);
    RYML_ASSERT(parent != NONE);
    RYML_ASSERT(after == NONE || has_child(parent, after));

    // don't loop using pointers as there may be a relocation

    // find the position where "after" is
    size_t after_pos = NONE;
    if(after != NONE)
    {
        for(size_t i = first_child(parent), icount = 0; i != NONE; ++icount, i = next_sibling(i))
        {
            if(i == after)
            {
                after_pos = icount;
                break;
            }
        }
        RYML_ASSERT(after_pos != NONE);
    }

    // for each child to be duplicated...
    size_t prev = after;
    for(size_t i = src->first_child(node), icount = 0; i != NONE; ++icount, i = src->next_sibling(i))
    {
        if(is_seq(parent))
        {
            prev = duplicate(i, parent, prev);
        }
        else
        {
            RYML_ASSERT(is_map(parent));
            // does the parent already have a node with key equal to that of the current duplicate?
            size_t rep = NONE, rep_pos = NONE;
            for(size_t j = first_child(parent), jcount = 0; j != NONE; ++jcount, j = next_sibling(j))
            {
                if(key(j) == key(i))
                {
                    rep = j;
                    rep_pos = jcount;
                    break;
                }
            }
            if(rep == NONE) // there is no repetition; just duplicate
            {
                prev = duplicate(src, i, parent, prev);
            }
            else  // yes, there is a repetition
            {
                if(after_pos != NONE && rep_pos < after_pos)
                {
                    // rep is located before the node which will be inserted,
                    // and will be overridden by the duplicate. So replace it.
                    remove(rep);
                    prev = duplicate(src, i, parent, prev);
                }
                else if(after_pos == NONE || rep_pos >= after_pos)
                {
                    // rep is located after the node which will be inserted
                    // and overrides it. So move the rep into this node's place.
                    if(rep != prev)
                    {
                        move(rep, prev);
                        prev = rep;
                    }
                }
            } // there's a repetition
        }
    }

    return prev;
}


//-----------------------------------------------------------------------------

void Tree::merge_with(Tree const *src, size_t src_node, size_t dst_node)
{
    RYML_ASSERT(src != nullptr);
    if(src_node == NONE)
        src_node = src->root_id();
    if(dst_node == NONE)
        dst_node = root_id();
    RYML_ASSERT(src->has_val(src_node) || src->is_seq(src_node) || src->is_map(src_node));

    if(src->has_val(src_node))
    {
        if( ! has_val(dst_node))
        {
            if(has_children(dst_node))
                remove_children(dst_node);
        }
        if(src->is_keyval(src_node))
            _copy_props(dst_node, src, src_node);
        else if(src->is_val(src_node))
            _copy_props_wo_key(dst_node, src, src_node);
        else
            C4_NEVER_REACH();
    }
    else if(src->is_seq(src_node))
    {
        if( ! is_seq(dst_node))
        {
            if(has_children(dst_node))
                remove_children(dst_node);
            _clear_type(dst_node);
            if(src->has_key(src_node))
                to_seq(dst_node, src->key(src_node));
            else
                to_seq(dst_node);
        }
        for(size_t sch = src->first_child(src_node); sch != NONE; sch = src->next_sibling(sch))
        {
            size_t dch = append_child(dst_node);
            _copy_props_wo_key(dch, src, sch);
            merge_with(src, sch, dch);
        }
    }
    else if(src->is_map(src_node))
    {
        if( ! is_map(dst_node))
        {
            if(has_children(dst_node))
                remove_children(dst_node);
            _clear_type(dst_node);
            if(src->has_key(src_node))
                to_map(dst_node, src->key(src_node));
            else
                to_map(dst_node);
        }
        for(size_t sch = src->first_child(src_node); sch != NONE; sch = src->next_sibling(sch))
        {
            size_t dch = find_child(dst_node, src->key(sch));
            if(dch == NONE)
            {
                dch = append_child(dst_node);
                _copy_props(dch, src, sch);
            }
            merge_with(src, sch, dch);
        }
    }
    else
    {
        C4_NEVER_REACH();
    }
}


//-----------------------------------------------------------------------------

namespace detail {
/** @todo make this part of the public API, refactoring as appropriate
 * to be able to use the same resolver to handle multiple trees (one
 * at a time) */
struct ReferenceResolver
{
    struct refdata
    {
        NodeType type;
        size_t node;
        size_t prev_anchor;
        size_t target;
        size_t parent_ref;
        size_t parent_ref_sibling;
    };

    Tree *t;
    /** from the specs: "an alias node refers to the most recent
     * node in the serialization having the specified anchor". So
     * we need to start looking upward from ref nodes.
     *
     * @see http://yaml.org/spec/1.2/spec.html#id2765878 */
    stack<refdata> refs;

    ReferenceResolver(Tree *t_) : t(t_), refs(t_->allocator())
    {
        resolve();
    }

    void store_anchors_and_refs()
    {
        // minimize (re-)allocations by counting first
        size_t num_anchors_and_refs = count_anchors_and_refs(t->root_id());
        if(!num_anchors_and_refs)
            return;
        refs.reserve(num_anchors_and_refs);

        // now descend through the hierarchy
        _store_anchors_and_refs(t->root_id());

        // finally connect the reference list
        size_t prev_anchor = npos;
        size_t count = 0;
        for(auto &rd : refs)
        {
            rd.prev_anchor = prev_anchor;
            if(rd.type.is_anchor())
                prev_anchor = count;
            ++count;
        }
    }

    size_t count_anchors_and_refs(size_t n)
    {
        size_t c = 0;
        c += t->has_key_anchor(n);
        c += t->has_val_anchor(n);
        c += t->is_key_ref(n);
        c += t->is_val_ref(n);
        for(size_t ch = t->first_child(n); ch != NONE; ch = t->next_sibling(ch))
            c += count_anchors_and_refs(ch);
        return c;
    }

    void _store_anchors_and_refs(size_t n)
    {
        if(t->is_key_ref(n) || t->is_val_ref(n) || (t->has_key(n) && t->key(n) == "<<"))
        {
            if(t->is_seq(n))
            {
                // for merging multiple inheritance targets
                //   <<: [ *CENTER, *BIG ]
                for(size_t ich = t->first_child(n); ich != NONE; ich = t->next_sibling(ich))
                {
                    RYML_ASSERT(t->num_children(ich) == 0);
                    refs.push({VALREF, ich, npos, npos, n, t->next_sibling(n)});
                }
                return;
            }
            if(t->is_key_ref(n) && t->key(n) != "<<") // insert key refs BEFORE inserting val refs
            {
                RYML_CHECK((!t->has_key(n)) || t->key(n).ends_with(t->key_ref(n)));
                refs.push({KEYREF, n, npos, npos, NONE, NONE});
            }
            if(t->is_val_ref(n))
            {
                RYML_CHECK((!t->has_val(n)) || t->val(n).ends_with(t->val_ref(n)));
                refs.push({VALREF, n, npos, npos, NONE, NONE});
            }
        }
        if(t->has_key_anchor(n))
        {
            RYML_CHECK(t->has_key(n));
            refs.push({KEYANCH, n, npos, npos, NONE, NONE});
        }
        if(t->has_val_anchor(n))
        {
            RYML_CHECK(t->has_val(n) || t->is_container(n));
            refs.push({VALANCH, n, npos, npos, NONE, NONE});
        }
        for(size_t ch = t->first_child(n); ch != NONE; ch = t->next_sibling(ch))
        {
            _store_anchors_and_refs(ch);
        }
    }

    size_t lookup_(refdata *C4_RESTRICT ra)
    {
        RYML_ASSERT(ra->type.is_key_ref() || ra->type.is_val_ref());
        RYML_ASSERT(ra->type.is_key_ref() != ra->type.is_val_ref());
        csubstr refname;
        if(ra->type.is_val_ref())
        {
            refname = t->val_ref(ra->node);
        }
        else
        {
            RYML_ASSERT(ra->type.is_key_ref());
            refname = t->key_ref(ra->node);
        }
        while(ra->prev_anchor != npos)
        {
            ra = &refs[ra->prev_anchor];
            if(t->has_anchor(ra->node, refname))
                return ra->node;
        }

#ifndef RYML_ERRMSG_SIZE
    #define RYML_ERRMSG_SIZE 1024
#endif
        char errmsg[RYML_ERRMSG_SIZE];
        snprintf(errmsg, RYML_ERRMSG_SIZE, "anchor does not exist: '%.*s'",
                 static_cast<int>(refname.size()), refname.data());
        c4::yml::error(errmsg);
        return NONE;
    }

    void resolve()
    {
        store_anchors_and_refs();
        if(refs.empty())
            return;

        /* from the specs: "an alias node refers to the most recent
         * node in the serialization having the specified anchor". So
         * we need to start looking upward from ref nodes.
         *
         * @see http://yaml.org/spec/1.2/spec.html#id2765878 */
        for(size_t i = 0, e = refs.size(); i < e; ++i)
        {
            auto &C4_RESTRICT rd = refs.top(i);
            if( ! rd.type.is_ref())
                continue;
            rd.target = lookup_(&rd);
        }
    }

}; // ReferenceResolver
} // namespace detail

void Tree::resolve()
{
    if(m_size == 0)
        return;

    detail::ReferenceResolver rr(this);

    // insert the resolved references
    size_t prev_parent_ref = NONE;
    size_t prev_parent_ref_after = NONE;
    for(auto const& C4_RESTRICT rd : rr.refs)
    {
        if( ! rd.type.is_ref())
            continue;
        if(rd.parent_ref != NONE)
        {
            RYML_ASSERT(is_seq(rd.parent_ref));
            size_t after, p = parent(rd.parent_ref);
            if(prev_parent_ref != rd.parent_ref)
            {
                after = rd.parent_ref;//prev_sibling(rd.parent_ref_sibling);
                prev_parent_ref_after = after;
            }
            else
            {
                after = prev_parent_ref_after;
            }
            prev_parent_ref = rd.parent_ref;
            prev_parent_ref_after = duplicate_children_no_rep(rd.target, p, after);
            remove(rd.node);
        }
        else
        {
            if(has_key(rd.node) && is_key_ref(rd.node) && key(rd.node) == "<<")
            {
                RYML_ASSERT(is_keyval(rd.node));
                size_t p = parent(rd.node);
                size_t after = prev_sibling(rd.node);
                duplicate_children_no_rep(rd.target, p, after);
                remove(rd.node);
            }
            else if(rd.type.is_key_ref())
            {
                RYML_ASSERT(is_key_ref(rd.node));
                RYML_ASSERT(has_key_anchor(rd.target) || has_val_anchor(rd.target));
                if(has_val_anchor(rd.target) && val_anchor(rd.target) == key_ref(rd.node))
                {
                    RYML_CHECK(!is_container(rd.target));
                    RYML_CHECK(has_val(rd.target));
                    _p(rd.node)->m_key.scalar = val(rd.target);
                    _add_flags(rd.node, KEY);
                }
                else
                {
                    RYML_CHECK(key_anchor(rd.target) == key_ref(rd.node));
                    _p(rd.node)->m_key.scalar = key(rd.target);
                    _add_flags(rd.node, VAL);
                }
            }
            else
            {
                RYML_ASSERT(rd.type.is_val_ref());
                if(has_key_anchor(rd.target) && key_anchor(rd.target) == val_ref(rd.node))
                {
                    RYML_CHECK(!is_container(rd.target));
                    RYML_CHECK(has_val(rd.target));
                    _p(rd.node)->m_val.scalar = key(rd.target);
                    _add_flags(rd.node, VAL);
                }
                else
                {
                    duplicate_contents(rd.target, rd.node);
                }
            }
        }
    }

    // clear anchors and refs
    for(auto const& C4_RESTRICT ar : rr.refs)
    {
        rem_anchor_ref(ar.node);
        if(ar.parent_ref != NONE)
            if(type(ar.parent_ref) != NOTYPE)
                remove(ar.parent_ref);
    }

}

//-----------------------------------------------------------------------------

size_t Tree::num_children(size_t node) const
{
    size_t count = 0;
    for(size_t i = first_child(node); i != NONE; i = next_sibling(i))
    {
        ++count;
    }
    return count;
}

size_t Tree::child(size_t node, size_t pos) const
{
    RYML_ASSERT(node != NONE);
    size_t count = 0;
    for(size_t i = first_child(node); i != NONE; i = next_sibling(i))
    {
        if(count++ == pos)
            return i;
    }
    return NONE;
}

size_t Tree::child_pos(size_t node, size_t ch) const
{
    size_t count = 0;
    for(size_t i = first_child(node); i != NONE; i = next_sibling(i))
    {
        if(i == ch)
            return count;
        ++count;
    }
    return npos;
}

#if defined(__clang__)
#   pragma clang diagnostic push
#   pragma GCC diagnostic ignored "-Wnull-dereference"
#elif defined(__GNUC__)
#   pragma GCC diagnostic push
#   if __GNUC__ >= 6
#       pragma GCC diagnostic ignored "-Wnull-dereference"
#   endif
#endif

size_t Tree::find_child(size_t node, csubstr const& name) const
{
    RYML_ASSERT(node != NONE);
    RYML_ASSERT(is_map(node));
    if(get(node)->m_first_child == NONE)
    {
        RYML_ASSERT(_p(node)->m_last_child == NONE);
        return NONE;
    }
    else
    {
        RYML_ASSERT(_p(node)->m_last_child != NONE);
    }
    for(size_t i = first_child(node); i != NONE; i = next_sibling(i))
    {
        if(_p(i)->m_key.scalar == name)
        {
            return i;
        }
    }
    return NONE;
}

#if defined(__clang__)
#   pragma clang diagnostic pop
#elif defined(__GNUC__)
#   pragma GCC diagnostic pop
#endif


//-----------------------------------------------------------------------------

void Tree::to_val(size_t node, csubstr const& val, type_bits more_flags)
{
    RYML_ASSERT( ! has_children(node));
    RYML_ASSERT(parent(node) == NONE || ! parent_is_map(node));
    _set_flags(node, VAL|more_flags);
    _p(node)->m_key.clear();
    _p(node)->m_val = val;
}

void Tree::to_keyval(size_t node, csubstr const& key, csubstr const& val, type_bits more_flags)
{
    RYML_ASSERT( ! has_children(node));
    RYML_ASSERT(parent(node) == NONE || parent_is_map(node));
    _set_flags(node, KEYVAL|more_flags);
    _p(node)->m_key = key;
    _p(node)->m_val = val;
}

void Tree::to_map(size_t node, type_bits more_flags)
{
    RYML_ASSERT( ! has_children(node));
    RYML_ASSERT(parent(node) == NONE || ! parent_is_map(node)); // parent must not have children with keys
    _set_flags(node, MAP|more_flags);
    _p(node)->m_key.clear();
    _p(node)->m_val.clear();
}

void Tree::to_map(size_t node, csubstr const& key, type_bits more_flags)
{
    RYML_ASSERT( ! has_children(node));
    RYML_ASSERT(parent(node) == NONE || parent_is_map(node));
    _set_flags(node, KEY|MAP|more_flags);
    _p(node)->m_key = key;
    _p(node)->m_val.clear();
}

void Tree::to_seq(size_t node, type_bits more_flags)
{
    RYML_ASSERT( ! has_children(node));
    RYML_ASSERT(parent(node) == NONE || parent_is_seq(node));
    _set_flags(node, SEQ|more_flags);
    _p(node)->m_key.clear();
    _p(node)->m_val.clear();
}

void Tree::to_seq(size_t node, csubstr const& key, type_bits more_flags)
{
    RYML_ASSERT( ! has_children(node));
    RYML_ASSERT(parent(node) == NONE || parent_is_map(node));
    _set_flags(node, KEY|SEQ|more_flags);
    _p(node)->m_key = key;
    _p(node)->m_val.clear();
}

void Tree::to_doc(size_t node, type_bits more_flags)
{
    RYML_ASSERT( ! has_children(node));
    _set_flags(node, DOC|more_flags);
    _p(node)->m_key.clear();
    _p(node)->m_val.clear();
}

void Tree::to_stream(size_t node, type_bits more_flags)
{
    RYML_ASSERT( ! has_children(node));
    _set_flags(node, STREAM|more_flags);
    _p(node)->m_key.clear();
    _p(node)->m_val.clear();
}


//-----------------------------------------------------------------------------

csubstr Tree::lookup_result::resolved() const
{
    csubstr p = path.first(path_pos);
    if(p.ends_with('.'))
        p = p.first(p.len-1);
    return p;
}

csubstr Tree::lookup_result::unresolved() const
{
    return path.sub(path_pos);
}

void Tree::_advance(lookup_result *r, size_t more) const
{
    r->path_pos += more;
    if(r->path.sub(r->path_pos).begins_with('.'))
        ++r->path_pos;
}

Tree::lookup_result Tree::lookup_path(csubstr path, size_t start) const
{
    if(start == NONE)
        start = root_id();
    lookup_result r(path, start);
    if(path.empty())
        return r;
    _lookup_path(&r);
    if(r.target == NONE && r.closest == start)
        r.closest = NONE;
    return r;
}

size_t Tree::lookup_path_or_modify(csubstr default_value, csubstr path, size_t start)
{
    size_t target = _lookup_path_or_create(path, start);
    if(parent_is_map(target))
        to_keyval(target, key(target), default_value);
    else
        to_val(target, default_value);
    return target;
}

size_t Tree::lookup_path_or_modify(Tree const *src, size_t src_node, csubstr path, size_t start)
{
    size_t target = _lookup_path_or_create(path, start);
    merge_with(src, src_node, target);
    return target;
}

size_t Tree::_lookup_path_or_create(csubstr path, size_t start)
{
    if(start == NONE)
        start = root_id();
    lookup_result r(path, start);
    _lookup_path(&r);
    if(r.target != NONE)
    {
        C4_ASSERT(r.unresolved().empty());
        return r.target;
    }
    _lookup_path_modify(&r);
    return r.target;
}

void Tree::_lookup_path(lookup_result *r) const
{
    C4_ASSERT( ! r->unresolved().empty());
    _lookup_path_token parent{"", type(r->closest)};
    size_t node;
    do
    {
        node = _next_node(r, &parent);
        if(node != NONE)
            r->closest = node;
        if(r->unresolved().empty())
        {
            r->target = node;
            return;
        }
    } while(node != NONE);
}

void Tree::_lookup_path_modify(lookup_result *r)
{
    C4_ASSERT( ! r->unresolved().empty());
    _lookup_path_token parent{"", type(r->closest)};
    size_t node;
    do
    {
        node = _next_node_modify(r, &parent);
        if(node != NONE)
            r->closest = node;
        if(r->unresolved().empty())
        {
            r->target = node;
            return;
        }
    } while(node != NONE);
}

size_t Tree::_next_node(lookup_result * r, _lookup_path_token *parent) const
{
    _lookup_path_token token = _next_token(r, *parent);
    if( ! token)
        return NONE;

    size_t node = NONE;
    csubstr prev = token.value;
    if(token.type == MAP || token.type == SEQ)
    {
        RYML_ASSERT(!token.value.begins_with('['));
        //RYML_ASSERT(is_container(r->closest) || r->closest == NONE);
        RYML_ASSERT(is_map(r->closest));
        node = find_child(r->closest, token.value);
    }
    else if(token.type == KEYVAL)
    {
        RYML_ASSERT(r->unresolved().empty());
        if(is_map(r->closest))
            node = find_child(r->closest, token.value);
    }
    else if(token.type == KEY)
    {
        RYML_ASSERT(token.value.begins_with('[') && token.value.ends_with(']'));
        token.value = token.value.offs(1, 1).trim(' ');
        size_t idx = 0;
        RYML_CHECK(from_chars(token.value, &idx));
        node = child(r->closest, idx);
    }
    else
    {
        C4_NEVER_REACH();
    }

    if(node != NONE)
    {
        *parent = token;
    }
    else
    {
        csubstr p = r->path.sub(r->path_pos > 0 ? r->path_pos - 1 : r->path_pos);
        r->path_pos -= prev.len;
        if(p.begins_with('.'))
            r->path_pos -= 1u;
    }

    return node;
}

size_t Tree::_next_node_modify(lookup_result * r, _lookup_path_token *parent)
{
    _lookup_path_token token = _next_token(r, *parent);
    if( ! token)
        return NONE;

    size_t node = NONE;
    if(token.type == MAP || token.type == SEQ)
    {
        RYML_ASSERT(!token.value.begins_with('['));
        //RYML_ASSERT(is_container(r->closest) || r->closest == NONE);
        if( ! is_container(r->closest))
        {
            if(has_key(r->closest))
                to_map(r->closest, key(r->closest));
            else
                to_map(r->closest);
        }
        else
        {
            if(is_map(r->closest))
                node = find_child(r->closest, token.value);
            else
            {
                size_t pos = NONE;
                RYML_CHECK(c4::atox(token.value, &pos));
                RYML_ASSERT(pos != NONE);
                node = child(r->closest, pos);
            }
        }
        if(node == NONE)
        {
            RYML_ASSERT(is_map(r->closest));
            node = append_child(r->closest);
            NodeData *n = _p(node);
            n->m_key.scalar = token.value;
            n->m_type.add(KEY);
        }
    }
    else if(token.type == KEYVAL)
    {
        RYML_ASSERT(r->unresolved().empty());
        if(is_map(r->closest))
        {
            node = find_child(r->closest, token.value);
            if(node == NONE)
                node = append_child(r->closest);
        }
        else
        {
            RYML_ASSERT(!is_seq(r->closest));
            _add_flags(r->closest, MAP);
            node = append_child(r->closest);
        }
        NodeData *n = _p(node);
        n->m_key.scalar = token.value;
        n->m_val.scalar = "";
        n->m_type.add(KEYVAL);
    }
    else if(token.type == KEY)
    {
        RYML_ASSERT(token.value.begins_with('[') && token.value.ends_with(']'));
        token.value = token.value.offs(1, 1).trim(' ');
        size_t idx;
        if( ! from_chars(token.value, &idx))
             return NONE;
        if( ! is_container(r->closest))
        {
            if(has_key(r->closest))
            {
                csubstr k = key(r->closest);
                _clear_type(r->closest);
                to_seq(r->closest, k);
            }
            else
            {
                _clear_type(r->closest);
                to_seq(r->closest);
            }
        }
        RYML_ASSERT(is_container(r->closest));
        node = child(r->closest, idx);
        if(node == NONE)
        {
            RYML_ASSERT(num_children(r->closest) <= idx);
            for(size_t i = num_children(r->closest); i <= idx; ++i)
            {
                node = append_child(r->closest);
                if(i < idx)
                {
                    if(is_map(r->closest))
                        to_keyval(node, /*"~"*/{}, /*"~"*/{});
                    else if(is_seq(r->closest))
                        to_val(node, /*"~"*/{});
                }
            }
        }
    }
    else
    {
        C4_NEVER_REACH();
    }

    RYML_ASSERT(node != NONE);
    *parent = token;
    return node;
}

/** types of tokens:
 * - seeing "map."  ---> "map"/MAP
 * - finishing "scalar" ---> "scalar"/KEYVAL
 * - seeing "seq[n]" ---> "seq"/SEQ (--> "[n]"/KEY)
 * - seeing "[n]" ---> "[n]"/KEY
 */
Tree::_lookup_path_token Tree::_next_token(lookup_result *r, _lookup_path_token const& parent) const
{
    csubstr unres = r->unresolved();
    if(unres.empty())
        return {};

    // is it an indexation like [0], [1], etc?
    if(unres.begins_with('['))
    {
        size_t pos = unres.find(']');
        if(pos == csubstr::npos)
            return {};
        csubstr idx = unres.first(pos + 1);
        _advance(r, pos + 1);
        return {idx, KEY};
    }

    // no. so it must be a name
    size_t pos = unres.first_of(".[");
    if(pos == csubstr::npos)
    {
        _advance(r, unres.len);
        NodeType t;
        if(( ! parent) || parent.type.is_seq())
            return {unres, VAL};
        return {unres, KEYVAL};
    }

    // it's either a map or a seq
    RYML_ASSERT(unres[pos] == '.' || unres[pos] == '[');
    if(unres[pos] == '.')
    {
        RYML_ASSERT(pos != 0);
        _advance(r, pos + 1);
        return {unres.first(pos), MAP};
    }

    RYML_ASSERT(unres[pos] == '[');
    _advance(r, pos);
    return {unres.first(pos), SEQ};
}

} // namespace ryml
} // namespace c4


C4_SUPPRESS_WARNING_GCC_POP
C4_SUPPRESS_WARNING_MSVC_POP
