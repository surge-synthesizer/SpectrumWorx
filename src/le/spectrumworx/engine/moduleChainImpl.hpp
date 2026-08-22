////////////////////////////////////////////////////////////////////////////////
///
/// \file moduleChainImpl.hpp
/// -------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef moduleChainImpl_hpp__E639274C_2DA1_42EC_9B43_AF42EBB9C987
#define moduleChainImpl_hpp__E639274C_2DA1_42EC_9B43_AF42EBB9C987
//------------------------------------------------------------------------------
#include "moduleNode.hpp"

#include "le/parameters/lfoImpl.hpp" //...mrmlj...only for the LFOImpl::Timer nested type...
#include "le/utility/platformSpecifics.hpp"

#include "le/utility/intrusivePtr.hpp"
#include "le/utility/circularListAlgorithms.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>

namespace LE::SW
{

namespace Engine
{
struct StorageFactors;
}
namespace Engine
{

class ModuleDSP;
class Setup;

////////////////////////////////////////////////////////////////////////////////
///
/// \class ModuleChainImpl
///
/// \note Using an intrusive container (with reference counted objects) because
/// of the implicit thread safety (as well as minor efficiency improvements).
///                                           (25.02.2014.) (Domagoj Saric)
///
////////////////////////////////////////////////////////////////////////////////

namespace Detail
{
struct module_node_traits
{
    using node = ModuleNode;
    using node_ptr = node *;
    using const_node_ptr = node const *;

    static node_ptr get_next(const_node_ptr n);
    static node_ptr get_previous(const_node_ptr n);
    static void set_next(node_ptr n, node_ptr next);
    static void set_previous(node_ptr n, node_ptr prev);
}; // struct module_node_traits

//...mrmlj...
/// \note Boost.Intrusive containers do not support shared ownership
/// semantics so we cannot use them yet (only the
/// boost::intrusive::circular_list_algorithms come in handy).
/// https://svn.boost.org/trac/boost/ticket/7003
///                                       (08.06.2012.) (Domagoj Saric)
/// \note And so only the algorithms were ever taken -- which is why stage 7
/// could reimplement them in eighty lines rather than keep a Boost dependency
/// for them. le/utility/circularListAlgorithms.hpp.
//struct module_node_value_traits
//{
//    typedef module_node_traits                     node_traits;
//    typedef node_traits::node                      node;
//    typedef node_traits::node_ptr                  node_ptr;
//    typedef node_traits::const_node_ptr            const_node_ptr;
//    typedef ModuleDSP                              value_type;
//    typedef ModulePtr                              pointer;
//    typedef ModuleCPtr                             const_pointer;
//    static const boost::intrusive::link_mode_type link_mode = boost::intrusive::safe_link;
//    static node_ptr       to_node_ptr ( value_type           & );
//    static const_node_ptr to_node_ptr ( value_type     const & );
//    static pointer        to_value_ptr( node_ptr       const & );
//    static const_pointer  to_value_ptr( const_node_ptr const & );
//};
} // namespace Detail

class ModuleChainBase :
    //...mrmlj...
    //: public boost::intrusive::list<ModuleDSP, boost::intrusive::value_traits<Detail::module_node_value_traits>>
    private ModuleNode
{
  public:                                              // Typedefs
    ModuleChainBase(ModuleChainBase const &) = delete; // makes non-copyable
    ModuleChainBase &operator=(ModuleChainBase const &) = delete;

    using difference_type = std::int8_t;
    using size_type = std::uint8_t;

    using Node = ModuleNode;
    using node_algorithms = LE::Utility::CircularListAlgorithms<Detail::module_node_traits>;

  public: // Iterator
    class chain_const_iterator : public Node::NodeCPtr
    {
      public:
        using Node = ModuleChainBase::Node const;
        using smart_ptr_t = Node::NodeCPtr;

        /// \note `std::iterator<std::bidirectional_iterator_tag, Node const,
        /// std::int8_t>` was a second base and is deprecated in C++17. The five
        /// typedefs it supplied are spelt out instead, the way chain_iterator
        /// below already spells its three.
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ModuleChainBase::difference_type;
        using value_type = Node;
        using pointer = value_type *;
        using reference = value_type &;

      public:
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wassume"
#endif // __clang__
        chain_const_iterator(decltype(nullptr) = nullptr) {}
        chain_const_iterator(value_type const *LE_RESTRICT const pointer) : smart_ptr_t(pointer)
        {
            LE_ASSUME(pointer);
        }
        chain_const_iterator(chain_const_iterator const &other) : smart_ptr_t(other)
        {
            LE_ASSUME(this->get());
        }
        chain_const_iterator(chain_const_iterator &&other)
            : smart_ptr_t(std::forward<smart_ptr_t>(other))
        {
            LE_ASSUME(this->get());
        }
        template <typename Source>
        chain_const_iterator(Source &&source) : smart_ptr_t(std::forward<Source>(source))
        {
            LE_ASSUME(this->get());
        }
        /// \note Returns the old position by value, as post-increment is
        /// defined to. It used to advance and then hand back
        /// `reinterpret_cast<chain_const_iterator &>( currentNode->previous_ )`
        /// -- a reference to the *node's* back link, read through an unrelated
        /// type. That is a strict aliasing violation (GCC 15 says so at -O3),
        /// and it handed the caller a writable alias of a live list link. It
        /// reached the right node only because the node it had just moved to
        /// points back at the one it came from. The copy costs one reference
        /// count round trip on a pointer the caller already holds.
        chain_const_iterator operator++(int)
        {
            chain_const_iterator const previousPosition(*this);
            ++(*this);
            return previousPosition;
        }
        chain_const_iterator &operator++()
        {
            Node *LE_RESTRICT const pThisNode(this->get());
            LE_ASSUME(pThisNode);
            Node *LE_RESTRICT const pNextNode(pThisNode->next_.get());
            LE_ASSUME(pNextNode);
            LE_ASSUME(pThisNode->referenceCount_ >=
                      1); // can be equal to 1 with iterators to a removed module...
            this->reset(pNextNode);
            LE_ASSUME(this->get() == pNextNode);
            return *this;
        }
        chain_const_iterator &operator--()
        {
            Node *LE_RESTRICT const pNode(this->get()->previous_.get());
            LE_ASSUME(pNode);
            this->reset(pNode);
            return *this;
        }
        reference operator*() const
        {
            Node *LE_RESTRICT const pNode(this->get());
            LE_ASSUME(pNode);
            return *pNode;
        }
        pointer operator->() const { return &this->operator*(); }
#ifdef __clang__
#pragma clang diagnostic pop
#endif // __clang__

        using smart_ptr_t::operator=;
    }; // class chain_const_iterator

    class chain_iterator //...mrmlj...
        : public chain_const_iterator
    {
      public:
        using Node = ModuleChainBase::Node;

        using value_type = Node;
        using pointer = value_type *;
        using reference = value_type &;

        using smart_ptr_t = LE::Utility::IntrusivePtr<Node>;

      public:
        chain_iterator(decltype(nullptr) = nullptr) {}
        chain_iterator(value_type *LE_RESTRICT const pointer) : chain_const_iterator(pointer) {}
        chain_iterator(chain_iterator const &other) : chain_const_iterator(other) {}
        chain_iterator(chain_iterator &&other)
            : chain_const_iterator(std::forward<chain_const_iterator>(other))
        {
        }
        template <typename Source>
        chain_iterator(Source &&source) : chain_const_iterator(std::forward<Source>(source))
        {
        }

        /// \note These three assign through the base the iterator actually has
        /// -- `IntrusivePtr<Node const>` -- rather than through a
        /// `smart_ptr_t &` (`IntrusivePtr<Node> &`) the object never contained.
        /// The conversion operator that produced the latter reinterpret_cast a
        /// reference to one class type as a reference to another, which is a
        /// strict aliasing violation GCC 15 reports at -O3, and it went with
        /// this. What is stored is a pointer either way; the constness of the
        /// pointee is what chain_iterator adds back, in get() and operator*().
        chain_iterator &operator=(pointer const pOther)
        {
            static_cast<chain_const_iterator::smart_ptr_t &>(*this) = pOther;
            return *this;
        }
        chain_iterator &operator=(smart_ptr_t const &pOther)
        {
            static_cast<chain_const_iterator::smart_ptr_t &>(*this) = pOther;
            return *this;
        }
        chain_iterator &operator=(chain_iterator const &pOther)
        {
            static_cast<chain_const_iterator::smart_ptr_t &>(*this) =
                static_cast<chain_const_iterator::smart_ptr_t const &>(pOther);
            return *this;
        }

        chain_iterator &operator--()
        {
            return static_cast<chain_iterator &>(chain_const_iterator::operator--());
        }
        chain_iterator &operator++()
        {
            return static_cast<chain_iterator &>(chain_const_iterator::operator++());
        }
        chain_iterator operator++(int)
        {
            chain_iterator const previousPosition(*this);
            ++(*this);
            return previousPosition;
        }

        reference operator*() const
        {
            return const_cast<reference>(chain_const_iterator::operator*());
        }
        pointer operator->() const { return &this->operator*(); }

        Node *get() const { return const_cast<Node *>(chain_const_iterator::get()); }
    }; // class chain_iterator

    using iterator = chain_iterator;
    using const_iterator = chain_const_iterator;

  public:
    ModuleChainBase();
    ModuleChainBase(ModuleChainBase &&);
    ~ModuleChainBase();

    ModuleChainBase &operator=(ModuleChainBase &&);

    /// \brief What `getIndexForModule()` answers about a module this chain does
    /// not hold. Past every real slot, so a `ParameterID` carrying it is refused
    /// rather than acted on. \see the definition.
    static std::uint8_t constexpr notInChain{static_cast<std::uint8_t>(-1)};

    std::uint8_t getIndexForModule(Node const &) const;
    template <class Module> std::uint8_t getIndexForModule(Module const &chainedModule) const
    {
        return getIndexForModule(node(chainedModule));
    }

    iterator module(std::uint8_t index);
    const_iterator module(std::uint8_t index) const;

    void moveModule(std::uint8_t sourceIndex, std::uint8_t targetIndex);

    std::uint8_t size() const;

    bool empty() const;

    void clear();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Exchanges the contents of two chains, without destroying anything.
    ///
    /// \note What publish-and-retire is made of. `operator=( && )` clears first,
    /// so it *destroys* the modules it displaces -- which on the audio thread is
    /// a `free()` inside the callback. This is three splices of a circular list
    /// and no ownership change at all: the caller hands over a chain holding the
    /// new modules and gets the same object back holding the old ones, to
    /// destroy wherever it likes.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void swap(ModuleChainBase &);

    static iterator iterator_to(Node &);
    static const_iterator iterator_to(Node const &);

    iterator begin();
    const_iterator begin() const;

    iterator end();
    const_iterator end() const;

    template <class Iterator> bool isEnd(Iterator const &pModule) const
    {
        return isEnd(static_cast<Node const *>(LE::Utility::getPointer(pModule)));
    }
    bool isEnd(Node const *) const;

    void push_back(Node &);

    void insertAtAndReplace(iterator const &pInsertPosition, Node &moduleToInsert);

    void remove(Node &);

    template <class ActualModule, class Functor> void forEach(Functor &&f)
    {
        //for ( auto & module : modules )
        //    f( module );

        iterator pModulePtr(this->begin());
        while (!isEnd(pModulePtr))
        {
            /// \note pModulePtr must remain pointing to the current module for
            /// the entire duration of the call to f in order to maintain its
            /// reference count (to keep it alive).
            ///                               (06.03.2014.) (Domagoj Saric)
            f(actualModule<ActualModule>(*pModulePtr));
            ++pModulePtr;
        }
    }

    template <class ActualModule, class Functor> void forEach(Functor &&f) const
    {
        const_cast<ModuleChainBase &>(*this).forEach<ActualModule const>(std::forward<Functor>(f));
    }

    //...mrmlj...quick-hack for sdk...
    Node &rootNode() { return *this; }
    Node const &rootNode() const { return *this; }

  private:
    void resetRoot();

    void moveAssign(ModuleChainBase &&);
}; // class ModuleChainBase

class ModuleChainImpl : public ModuleChainBase
{
  public: // Typedefs
    using Module = ModuleDSP;

    using value_type = Module;
    using reference = value_type &;
    using const_reference = value_type const &;
    using pointer = LE::Utility::IntrusivePtr<Module>;
    using const_pointer = LE::Utility::IntrusivePtr<Module const>;

  public:
#if defined(_MSC_VER) && _MSC_VER < 1900
    ModuleChainImpl() {}
    ModuleChainImpl(ModuleChainImpl &&other) : ModuleChainBase(std::forward<ModuleChainBase>(other))
    {
    }
#else
    using ModuleChainBase::ModuleChainBase;
#endif // _MSC_VER
    using ModuleChainBase::operator=;

    void preProcessAll(Parameters::LFOImpl::Timer const &, Setup const &);

    /// \brief Resets every module, and on the way gives each of them the random
    /// streams they own. \see ModuleDSP::seedRandomState().
    void resetAll(Math::Rng &seedSource);

    bool resizeAll(StorageFactors const &newfactors, StorageFactors const &currentFactors);
}; // class ModuleChainImpl

} // namespace Engine

} // namespace LE::SW

#endif // moduleChainImpl_hpp
