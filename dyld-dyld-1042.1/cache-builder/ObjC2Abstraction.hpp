/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*- 
 *
 * Copyright (c) 2008-2010 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#include <iterator>
#include <deque>
#include <optional>
#include <set>


// iterate an entsize-based list
// typedef entsize_iterator<P, type_t<P>, type_list_t<P> > type_iterator;
template <typename P, typename T, typename Tlist>
struct entsize_iterator {
    uint32_t entsize;
    uint32_t index;  // keeping track of this saves a divide in operator-
    T* current;    

    typedef std::random_access_iterator_tag iterator_category;
    typedef T value_type;
    typedef ptrdiff_t difference_type;
    typedef T* pointer;
    typedef T& reference;
    
    entsize_iterator() { } 
    
    entsize_iterator(const Tlist& list, uint32_t start = 0)
        : entsize(list.getEntsize()), index(start), current((T*)list.get(start))
    { }
    
    const entsize_iterator<P,T,Tlist>& operator += (ptrdiff_t count) {
        current = (T*)((uint8_t *)current + count*entsize);
        index += count;
        return *this;
    }
    const entsize_iterator<P,T,Tlist>& operator -= (ptrdiff_t count) {
        current = (T*)((uint8_t *)current - count*entsize);
        index -= count;
        return *this;
    }
    const entsize_iterator<P,T,Tlist> operator + (ptrdiff_t count) const {
        return entsize_iterator(*this) += count;
    }
    const entsize_iterator<P,T,Tlist> operator - (ptrdiff_t count) const {
        return entsize_iterator(*this) -= count;
    }
    
    entsize_iterator<P,T,Tlist>& operator ++ () { *this += 1; return *this; }
    entsize_iterator<P,T,Tlist>& operator -- () { *this -= 1; return *this; }
    entsize_iterator<P,T,Tlist> operator ++ (int) { 
        entsize_iterator<P,T,Tlist> result(*this); *this += 1; return result; 
    }
    entsize_iterator<P,T,Tlist> operator -- (int) { 
        entsize_iterator<P,T,Tlist> result(*this); *this -= 1; return result; 
    }
    
    ptrdiff_t operator - (const entsize_iterator<P,T,Tlist>& rhs) const {
        return (ptrdiff_t)this->index - (ptrdiff_t)rhs.index;
    }
    
    T& operator * () { return *current; }
    T& operator * () const { return *current; }
    T& operator -> () { return *current; }
    const T& operator -> () const { return *current; }
    
    operator T& () const { return *current; }
    
    bool operator == (const entsize_iterator<P,T,Tlist>& rhs) {
        return this->current == rhs.current;
    }
    bool operator != (const entsize_iterator<P,T,Tlist>& rhs) {
        return this->current != rhs.current;
    }
    
    bool operator < (const entsize_iterator<P,T,Tlist>& rhs) {
        return this->current < rhs.current;
    }        
    bool operator > (const entsize_iterator<P,T,Tlist>& rhs) {
        return this->current > rhs.current;
    }

    
    static void overwrite(entsize_iterator<P,T,Tlist>& dst, const Tlist* srcList)
    {
        entsize_iterator<P,T,Tlist> src;
        uint32_t ee = srcList->getEntsize();
        for (src = srcList->begin(); src != srcList->end(); ++src) {
            memcpy(&*dst, &*src, ee);
            ++dst;
        }
    }
};

template <typename P> 
class objc_header_info_rw_t {

    typedef typename P::uint_t pint_t;

    pint_t data;   // loaded:1, allRealised:1, objc_header_info *:ptr

public:
    objc_header_info_rw_t(ContentAccessor* cache, const macho_header<P>* mh)
        : data(0) {
    }
};

template <typename P>
class objc_header_info_ro_t {

    typedef typename P::uint_t pint_t;

    pint_t mhdr_offset;     // offset to mach_header or mach_header_64
    pint_t info_offset;     // offset to objc_image_info *

public:
    objc_header_info_ro_t(ContentAccessor* cache, const macho_header<P>* mh)
        : mhdr_offset(0), info_offset(0) {
        P::setP(mhdr_offset, (uint64_t)cache->vmAddrForContent((void*)mh) - (uint64_t)cache->vmAddrForContent(&mhdr_offset));
        assert(header_vmaddr(cache) == (uint64_t)cache->vmAddrForContent((void*)mh));
        const macho_section<P>* sect = mh->getSection("__DATA", "__objc_imageinfo");
        if (sect) {
            P::setP(info_offset, (uint64_t)sect->addr() - (uint64_t)cache->vmAddrForContent(&info_offset));
            // set bit in mach_header.flags to tell dyld that this image has objc content
            macho_header<P>* rwmh = const_cast<macho_header<P>*>(mh);
            rwmh->set_flags(mh->flags() | MH_HAS_OBJC);
        }
        else
            P::setP(info_offset, - (uint64_t)cache->vmAddrForContent(&info_offset));
    }

    pint_t header_vmaddr(ContentAccessor* cache) const {
        return (pint_t)(((uint64_t)cache->vmAddrForContent(&mhdr_offset)) + mhdr_offset);
    }
};

template <typename P>
class objc_method_list_t {

    typedef typename P::uint_t pint_t;

    template <typename PtrTy>
    class objc_method_small_t {
        typedef typename PtrTy::uint_t pint_t;
        int32_t name;   // SEL
        int32_t types;  // const char *
        int32_t imp;    // IMP
        friend class objc_method_list_t<PtrTy>;

        objc_method_small_t() = delete;
        ~objc_method_small_t() = delete;
        objc_method_small_t(const objc_method_small_t& other) = delete;
        objc_method_small_t(objc_method_small_t&& other) = delete;
        objc_method_small_t& operator=(const objc_method_small_t& other) = delete;
        objc_method_small_t& operator=(objc_method_small_t&& other) = delete;

    public:

        pint_t getName(ContentAccessor* cache, std::optional<uint64_t> isOffsetFromBase) const {
            // We want to return the VM address of the "const char*" our selector
            // reference is pointing at.
            if ( isOffsetFromBase.has_value() ) {
                // Offset is from base directly to the SEL, not a selRef
                return (pint_t)(*isOffsetFromBase + name);
            } else {
                pint_t* nameRef = (pint_t*)((uint8_t*)&name + name);
                return (pint_t)PtrTy::getP(*nameRef);
            }
        }
        // We want to update the selRef we are pointing at with the new content
        // We may share the same selRef with other method lists or @SEL expressions, but as
        // all of them want the same uniqued selector anyway, its safe to overwrite it here for
        // everyone.
        void setName(ContentAccessor* cache, pint_t newNameVMAddr, std::optional<uint64_t> isOffsetFromBase) {
            if ( isOffsetFromBase.has_value() ) {
                // Offset is from base directly to the SEL, not a selRef
                intptr_t nameOffset = (intptr_t)(newNameVMAddr - *isOffsetFromBase);
                int32_t encodedNameOffset = (int32_t)nameOffset;
                assert(encodedNameOffset == nameOffset);
                this->name = encodedNameOffset;
            } else {
                pint_t* selRef = (pint_t*)((uint8_t*)&name + name);
                PtrTy::setP(*selRef, newNameVMAddr);
            }
        }
        // Returns the vmAddr of the types
        pint_t getTypes(ContentAccessor* cache) const {
            pint_t* typesRef = (pint_t*)((uint8_t*)&types + types);
            return (pint_t)cache->vmAddrForContent(typesRef);
        }
        void setTypes(ContentAccessor* cache, pint_t newTypesVMAddr) {
            void* typesPtr = cache->contentForVMAddr(newTypesVMAddr);
            this->types = (int32_t)(intptr_t)((uint8_t*)typesPtr - (uint8_t*)&this->types);
        }
        // Returns the vmAddr of the IMP
        pint_t getIMP(ContentAccessor* cache) const {
            pint_t* impRef = (pint_t*)((uint8_t*)&imp + imp);
            return (pint_t)cache->vmAddrForContent(impRef);
        }
        void setIMP(ContentAccessor* cache, pint_t newIMPVMAddr) {
            void* impPtr = cache->contentForVMAddr(newIMPVMAddr);
            this->imp = (int32_t)(intptr_t)((uint8_t*)impPtr - (uint8_t*)&this->imp);
        }

        // Swap the contents of this value and other
        // This has to recompute all of the relative offsets
        void swap(objc_method_small_t<PtrTy>* other) {
            // Get our targets
            uint8_t* ourNameTarget  = (uint8_t*)&this->name + this->name;
            uint8_t* ourTypesTarget = (uint8_t*)&this->types + this->types;
            uint8_t* ourIMPTarget   = (uint8_t*)&this->imp + this->imp;
            // Get their targets
            uint8_t* theirNameTarget  = (uint8_t*)&other->name + other->name;
            uint8_t* theirTypesTarget = (uint8_t*)&other->types + other->types;
            uint8_t* theirIMPTarget   = (uint8_t*)&other->imp + other->imp;
            // Set our targets
            this->name = (int32_t)(intptr_t)(theirNameTarget - (uint8_t*)&this->name);
            this->types = (int32_t)(intptr_t)(theirTypesTarget - (uint8_t*)&this->types);
            this->imp = (int32_t)(intptr_t)(theirIMPTarget - (uint8_t*)&this->imp);
            // Set their targets
            other->name = (int32_t)(intptr_t)(ourNameTarget - (uint8_t*)&other->name);
            other->types = (int32_t)(intptr_t)(ourTypesTarget - (uint8_t*)&other->types);
            other->imp = (int32_t)(intptr_t)(ourIMPTarget - (uint8_t*)&other->imp);
        }

        struct SortBySELAddress :
            public std::binary_function<const objc_method_small_t<PtrTy>&,
                                        const objc_method_small_t<PtrTy>&, bool>
        {
            SortBySELAddress(ContentAccessor* cache, std::optional<uint64_t> isOffsetFromBase)
                : cache(cache), isOffsetFromBase(isOffsetFromBase) { }

            bool operator() (const objc_method_small_t<PtrTy>& lhs,
                             const objc_method_small_t<PtrTy>& rhs)
            {
                return lhs.getName(cache, isOffsetFromBase) < rhs.getName(cache, isOffsetFromBase);
            }

            ContentAccessor*        cache               = nullptr;
            std::optional<uint64_t> isOffsetFromBase    = {};
        };
    };

    template <typename PtrTy>
    class objc_method_large_t {
        typedef typename PtrTy::uint_t pint_t;
        pint_t name;   // SEL
        pint_t types;  // const char *
        pint_t imp;    // IMP
        friend class objc_method_list_t<PtrTy>;
    public:
        pint_t getName() const {
            return (pint_t)PtrTy::getP(name);
        }
        void setName(pint_t newName) {
            PtrTy::setP(name, newName);
        }
        pint_t getTypes() const {
            return (pint_t)PtrTy::getP(types);
        }
        void setTypes(pint_t newTypes) {
            PtrTy::setP(types, newTypes);
        }
        pint_t getIMP() const {
            return (pint_t)PtrTy::getP(imp);
        }
        void setIMP(pint_t newIMP) {
            PtrTy::setP(imp, newIMP);
        }

        struct SortBySELAddress :
            public std::binary_function<const objc_method_large_t<PtrTy>&,
                                        const objc_method_large_t<PtrTy>&, bool>
        {
            bool operator() (const objc_method_large_t<PtrTy>& lhs,
                             const objc_method_large_t<PtrTy>& rhs)
            {
                return lhs.getName() < rhs.getName();
            }
        };
    };

    // Temporary struct to use when sorting small methods as their int32_t offsets can't reach
    // from the stack where temporary values are placed, in to the shared cache buffer where the data lives
    struct TempMethod {
        // Relative methods in the shared cache always use direct offsets to the SEL
        // at the point where this is running.  That means we don't need to indirect through
        // a SEL reference.
        pint_t selVMAddr;
        pint_t typesVMAddr;
        pint_t impVMAddr;
    };

    template <typename PtrTy>
    struct SortBySELAddress :
        public std::binary_function<const TempMethod&,
                                    const TempMethod&, bool>
    {
        SortBySELAddress(ContentAccessor* cache) : cache(cache) { }

        bool operator() (const TempMethod& lhs,
                         const TempMethod& rhs)
        {
            return lhs.selVMAddr < rhs.selVMAddr;
        }

        ContentAccessor* cache = nullptr;
    };

    uint32_t entsize;
    uint32_t count;
    union {
        objc_method_small_t<P> small;
        objc_method_large_t<P> large;
    } first;

    void* operator new (size_t, void* buf) { return buf; }

    enum : uint32_t {
        // If this is set, the relative method lists name_offset field is an
        // offset directly to the SEL, not a SEL ref.
        relativeMethodSelectorsAreDirectFlag    = 0x40000000,

        // If this is set, then method lists are the new relative format, not
        // the old pointer based format
        relativeMethodFlag                      = 0x80000000,

        // The upper 16-bits are all defined to be flags
        methodListFlagsMask                     = 0xFFFF0000
    };

    uint32_t getFlags() const {
        return (P::E::get32(entsize) & methodListFlagsMask);
    }

    typedef entsize_iterator<P, objc_method_small_t<P>, objc_method_list_t<P> > small_method_iterator;
    typedef entsize_iterator<P, objc_method_large_t<P>, objc_method_list_t<P> > large_method_iterator;

    small_method_iterator beginSmall() {
        assert(usesRelativeMethods());
        return small_method_iterator(*this, 0);
    }
    small_method_iterator endSmall() {
        assert(usesRelativeMethods());
        return small_method_iterator(*this, getCount());
    }

    large_method_iterator beginLarge() {
        assert(!usesRelativeMethods());
        return large_method_iterator(*this, 0);
    }
    large_method_iterator endLarge() {
        assert(!usesRelativeMethods());
        return large_method_iterator(*this, getCount());
    }

public:

    uint32_t getCount() const { return P::E::get32(count); }

    uint32_t getEntsize() const {
        return P::E::get32(entsize) & ~(uint32_t)3 & ~methodListFlagsMask;
    }

    uint32_t byteSize() const { 
        return byteSizeForCount(getCount(), getEntsize()); 
    }

    static uint32_t byteSizeForCount(uint32_t c, uint32_t e) {
        return sizeof(entsize) + sizeof(count) + c*e;
    }

    bool usesRelativeMethods() const {
        return (P::E::get32(entsize) & relativeMethodFlag) != 0;
    }

    void setFixedUp() {
        P::E::set32(entsize, getEntsize() | 3 | getFlags());
    }

    void setMethodListSelectorsAreDirect() {
        P::E::set32(entsize, getEntsize() | getFlags() | relativeMethodSelectorsAreDirectFlag);
    }

    void sortMethods(ContentAccessor* cache, pint_t *typelist, std::optional<uint64_t> isOffsetFromBase) {
        if ( usesRelativeMethods() ) {
            // At this point we assume we are using offsets directly to selectors.  This
            // is so that the TempMethod struct can also use direct offsets and not track the
            // SEL reference VMAddrs
            assert(isOffsetFromBase.has_value());

            if ( typelist == nullptr ) {
                // This is the case when we are sorting the methods on a class.
                // Only protocols have a type list which causes the other sort to be used
                // We can't sort the small methods in place as their 32-bit offsets can't reach
                // the VM space where the shared cache is being created.  Instead create a list
                // of large methods and sort those.

                std::vector<TempMethod> largeMethods;
                for (unsigned i = 0 ; i != count; ++i) {
                    const objc_method_small_t<P>* smallMethod = (const objc_method_small_t<P>*)get(i);
                    TempMethod largeMethod;
                    largeMethod.selVMAddr = smallMethod->getName(cache, isOffsetFromBase);
                    largeMethod.typesVMAddr = smallMethod->getTypes(cache);
                    largeMethod.impVMAddr = smallMethod->getIMP(cache);
                    largeMethods.push_back(largeMethod);
                }

                SortBySELAddress<P> sorter(cache);
                std::stable_sort(largeMethods.begin(), largeMethods.end(), sorter);

                for (unsigned i = 0 ; i != count; ++i) {
                    const TempMethod& largeMethod = largeMethods[i];
                    objc_method_small_t<P>* smallMethod = (objc_method_small_t<P>*)get(i);
                    smallMethod->setName(cache, largeMethod.selVMAddr, isOffsetFromBase);
                    smallMethod->setTypes(cache, largeMethod.typesVMAddr);
                    smallMethod->setIMP(cache, largeMethod.impVMAddr);
                }

#if 0
                // Check the method lists are sorted
                {
                    typename objc_method_small_t<P>::SortBySELAddress sorter(cache);
                    for (uint32_t i = 0; i < getCount(); i++) {
                        for (uint32_t j = i+1; j < getCount(); j++) {
                            objc_method_small_t<P>* mi = (objc_method_small_t<P>*)get(i);
                            objc_method_small_t<P>* mj = (objc_method_small_t<P>*)get(j);
                            if ( mi->getName(cache) == mj->getName(cache) )
                                continue;
                            if (! sorter(*mi, *mj)) {
                                assert(false);
                            }
                        }
                    }
                }
#endif
            }
            else {
                typename objc_method_small_t<P>::SortBySELAddress sorter(cache, isOffsetFromBase);
                // can't easily use std::stable_sort here
                for (uint32_t i = 0; i < getCount(); i++) {
                    for (uint32_t j = i+1; j < getCount(); j++) {
                        objc_method_small_t<P>* mi = (objc_method_small_t<P>*)get(i);
                        objc_method_small_t<P>* mj = (objc_method_small_t<P>*)get(j);
                        if (! sorter(*mi, *mj)) {
                            mi->swap(mj);
                            if (typelist) std::swap(typelist[i], typelist[j]);
                        }
                    }
                }
            }
        } else {
            typename objc_method_large_t<P>::SortBySELAddress sorter;

            if ( typelist == nullptr ) {
                // This is the case when we are sorting the methods on a class.
                // Only protocols have a type list which causes the other sort to be used
                std::stable_sort(beginLarge(), endLarge(), sorter);
            }
            else {
                // can't easily use std::stable_sort here
                for (uint32_t i = 0; i < getCount(); i++) {
                    for (uint32_t j = i+1; j < getCount(); j++) {
                        objc_method_large_t<P>* mi = (objc_method_large_t<P>*)get(i);
                        objc_method_large_t<P>* mj = (objc_method_large_t<P>*)get(j);
                        if (! sorter(*mi, *mj)) {
                            std::swap(*mi, *mj);
                            if (typelist) std::swap(typelist[i], typelist[j]);
                        }
                    }
                }
            }
        }
        // mark method list as sorted
        this->setFixedUp();
    }

    pint_t getName(ContentAccessor* cache, uint32_t i, std::optional<uint64_t> isOffsetFromBase) {
        pint_t name = 0;
        if ( usesRelativeMethods() ) {
            small_method_iterator it = beginSmall() + i;
            objc_method_small_t<P>& method = *it;
            name = method.getName(cache, isOffsetFromBase);
        } else {
            large_method_iterator it = beginLarge() + i;
            objc_method_large_t<P>& method = *it;
            name = method.getName();
        }
        return name;
    }

    void setName(ContentAccessor* cache, uint32_t i, pint_t name, std::optional<uint64_t> isOffsetFromBase) {
        if ( usesRelativeMethods() ) {
            small_method_iterator it = beginSmall() + i;
            objc_method_small_t<P>& method = *it;
            method.setName(cache, name, isOffsetFromBase);
        } else {
            large_method_iterator it = beginLarge() + i;
            objc_method_large_t<P>& method = *it;
            method.setName(name);
        }
    }

    const char* getStringName(ContentAccessor* cache, uint32_t i, std::optional<uint64_t> isOffsetFromBase) {
        return (const char*)cache->contentForVMAddr(getName(cache, i, isOffsetFromBase));
    }

    pint_t getImp(uint32_t i, ContentAccessor* cache) {
        pint_t name = 0;
        if ( usesRelativeMethods() ) {
            small_method_iterator it = beginSmall() + i;
            objc_method_small_t<P>& method = *it;
            name = method.getIMP(cache);
        } else {
            large_method_iterator it = beginLarge() + i;
            objc_method_large_t<P>& method = *it;
            name = method.getIMP();
        }
        return name;
    }

    void* get(uint32_t i) const {
        if ( usesRelativeMethods() ) {
            return (void*)(objc_method_small_t<P> *)((uint8_t *)&first + i * getEntsize());
        } else {
            return (void*)(objc_method_large_t<P> *)((uint8_t *)&first + i * getEntsize());
        }
    }

    void operator delete(void * p) { 
        ::free(p); 
    }

private:

    // use newMethodList instead
    void* operator new (size_t);
};


template <typename P>
class objc_ivar_t {
    typedef typename P::uint_t pint_t;

    pint_t                    offset;  // uint32_t*  (uint64_t* on x86_64)
    pint_t                    name;    // const char*
    pint_t                    type;    // const char*
    uint32_t                alignment;
    uint32_t                size;
    
public:
    const char* getName(ContentAccessor* cache) const { return (const char *)cache->contentForVMAddr(P::getP(name)); }

    bool hasOffset() const { return offset != 0; }
    uint32_t getOffset(ContentAccessor* cache) const { return P::E::get32(*(uint32_t*)(cache->contentForVMAddr(P::getP(offset)))); }
    void setOffset(ContentAccessor* cache, uint32_t newOffset) { P::E::set32(*(uint32_t*)(cache->contentForVMAddr(P::getP(offset))), newOffset); }


    uint32_t getAlignment() {
        uint32_t a = P::E::get32(alignment);
        return (a == (uint32_t)-1) ? sizeof(pint_t) : 1<<a;
    }
};

template <typename P>
class objc_ivar_list_t {
    typedef typename P::uint_t pint_t;
    uint32_t entsize;
    uint32_t count;
    objc_ivar_t<P> first;

    void* operator new (size_t, void* buf) { return buf; }

public:

    typedef entsize_iterator<P, objc_ivar_t<P>, objc_ivar_list_t<P> > ivar_iterator;

    uint32_t getCount() const { return P::E::get32(count); }

    uint32_t getEntsize() const { return P::E::get32(entsize); }

    void* get(pint_t i) const { return (void*)(objc_ivar_t<P> *)((uint8_t *)&first + i * P::E::get32(entsize)); }

    uint32_t byteSize() const { 
        return byteSizeForCount(getCount(), getEntsize()); 
    }

    static uint32_t byteSizeForCount(uint32_t c, uint32_t e = sizeof(objc_ivar_t<P>)) { 
        return sizeof(objc_ivar_list_t<P>) - sizeof(objc_ivar_t<P>) + c*e;
    }

    ivar_iterator begin() { return ivar_iterator(*this, 0); }
    ivar_iterator end() { return ivar_iterator(*this, getCount()); }
    const ivar_iterator begin() const { return ivar_iterator(*this, 0); }
    const ivar_iterator end() const { return ivar_iterator(*this, getCount()); }

    static objc_ivar_list_t<P>* newIvarList(size_t newCount, uint32_t newEntsize) {
        void *buf = ::calloc(byteSizeForCount(newCount, newEntsize), 1);
        return new (buf) objc_ivar_list_t<P>(newCount, newEntsize);
    }

    void operator delete(void * p) { 
        ::free(p); 
    }

    objc_ivar_list_t(uint32_t newCount, 
                         uint32_t newEntsize = sizeof(objc_ivar_t<P>))
        : entsize(newEntsize), count(newCount) 
    { }
private:
    // use newIvarList instead
    void* operator new (size_t);
};


template <typename P> class objc_property_list_t; // forward 

template <typename P>
class objc_property_t {
    typedef typename P::uint_t pint_t;
    pint_t name;
    pint_t attributes;
    friend class objc_property_list_t<P>;
public:
    
    const char * getName(ContentAccessor* cache) const { return (const char *)cache->contentForVMAddr(P::getP(name)); }

    const char * getAttributes(ContentAccessor* cache) const { return (const char *)cache->contentForVMAddr(P::getP(attributes)); }
};

template <typename P>
class objc_property_list_t {
    uint32_t entsize;
    uint32_t count;
    objc_property_t<P> first;

    void* operator new (size_t, void* buf) { return buf; }

public:

    typedef entsize_iterator<P, objc_property_t<P>, objc_property_list_t<P> > property_iterator;

    uint32_t getCount() const { return P::E::get32(count); }

    uint32_t getEntsize() const { return P::E::get32(entsize); }

    void* get(uint32_t i) const { return (objc_property_t<P> *)((uint8_t *)&first + i * getEntsize()); }

    uint32_t byteSize() const { 
        return byteSizeForCount(getCount(), getEntsize()); 
    }

    static uint32_t byteSizeForCount(uint32_t c, uint32_t e = sizeof(objc_property_t<P>)) { 
        return sizeof(objc_property_list_t<P>) - sizeof(objc_property_t<P>) + c*e;
    }

    property_iterator begin() { return property_iterator(*this, 0); }
    property_iterator end() { return property_iterator(*this, getCount()); }
    const property_iterator begin() const { return property_iterator(*this, 0); }
    const property_iterator end() const { return property_iterator(*this, getCount()); }

    void getPointers(std::set<void*>& pointersToRemove) {
        for(property_iterator it = begin(); it != end(); ++it) {
            objc_property_t<P>& entry = *it;
            pointersToRemove.insert(&(entry.name));
            pointersToRemove.insert(&(entry.attributes));
        }
    }
#if 0
    static void addPointers(uint8_t* propertyList, CacheBuilder::ASLR_Tracker& aslrTracker) {
        objc_property_list_t<P>* plist = (objc_property_list_t<P>*)propertyList;
        for(property_iterator it = plist->begin(); it != plist->end(); ++it) {
            objc_property_t<P>& entry = *it;
            aslrTracker.add(&(entry.name));
            aslrTracker.add(&(entry.attributes));
        }
    }
#endif
     static objc_property_list_t<P>* newPropertyList(size_t newCount, uint32_t newEntsize) {
        void *buf = ::calloc(byteSizeForCount(newCount, newEntsize), 1);
        return new (buf) objc_property_list_t<P>(newCount, newEntsize);
    }

    void operator delete(void * p) { 
        ::free(p); 
    }

    objc_property_list_t(uint32_t newCount, 
                         uint32_t newEntsize = sizeof(objc_property_t<P>))
        : entsize(newEntsize), count(newCount) 
    { }
private:
    // use newPropertyList instead
    void* operator new (size_t);
};


template <typename A> class objc_protocol_list_t;  // forward reference

template <typename P>
class objc_protocol_t {
    typedef typename P::uint_t pint_t;

    pint_t isa;
    pint_t name;
    pint_t protocols;
    pint_t instanceMethods;
    pint_t classMethods;
    pint_t optionalInstanceMethods;
    pint_t optionalClassMethods;
    pint_t instanceProperties;
    uint32_t size;
    uint32_t flags;
    pint_t extendedMethodTypes;
    pint_t demangledName;
    pint_t classProperties;

public:
    pint_t getIsaVMAddr() const { return (pint_t)P::getP(isa); }
    void setIsaVMAddr(pint_t newIsa) { P::setP(isa, newIsa); }
    void* getISALocation() const { return (void*)&isa; }

    const char *getName(ContentAccessor* cache) const { return (const char *)cache->contentForVMAddr(P::getP(name)); }

    uint32_t getSize() const { return P::E::get32(size); }
    void setSize(uint32_t newSize) { P::E::set32(size, newSize); }

    uint32_t getFlags() const { return P::E::get32(flags); }

    void setFixedUp() { P::E::set32(flags, getFlags() | (1<<30)); }
    void setIsCanonical() {
        assert((getFlags() & (1 << 29)) == 0);
        P::E::set32(flags, getFlags() | (1<<29));
    }

    objc_protocol_list_t<P> *getProtocols(ContentAccessor* cache) const { return (objc_protocol_list_t<P> *)cache->contentForVMAddr(P::getP(protocols)); }

    objc_method_list_t<P> *getInstanceMethods(ContentAccessor* cache) const { return (objc_method_list_t<P> *)cache->contentForVMAddr(P::getP(instanceMethods)); }

    objc_method_list_t<P> *getClassMethods(ContentAccessor* cache) const { return (objc_method_list_t<P> *)cache->contentForVMAddr(P::getP(classMethods)); }

    objc_method_list_t<P> *getOptionalInstanceMethods(ContentAccessor* cache) const { return (objc_method_list_t<P> *)cache->contentForVMAddr(P::getP(optionalInstanceMethods)); }

    objc_method_list_t<P> *getOptionalClassMethods(ContentAccessor* cache) const { return (objc_method_list_t<P> *)cache->contentForVMAddr(P::getP(optionalClassMethods)); }

    objc_property_list_t<P> *getInstanceProperties(ContentAccessor* cache) const { return (objc_property_list_t<P> *)cache->contentForVMAddr(P::getP(instanceProperties)); }

    pint_t *getExtendedMethodTypes(ContentAccessor* cache) const {
        if (getSize() < offsetof(objc_protocol_t<P>, extendedMethodTypes) + sizeof(extendedMethodTypes)) {
            return NULL;
        }
        return (pint_t *)cache->contentForVMAddr(P::getP(extendedMethodTypes));
    }

    const char *getDemangledName(ContentAccessor* cache) const {
        if (sizeof(*this) < offsetof(objc_protocol_t<P>, demangledName) + sizeof(demangledName)) {
            return NULL;
        }
        return (const char *)cache->contentForVMAddr(P::getP(demangledName));
    }

    void setDemangledName(ContentAccessor* cache, const char *newName, Diagnostics& diag) {
        if (sizeof(*this) < offsetof(objc_protocol_t<P>, demangledName) + sizeof(demangledName))
            diag.error("objc protocol has the wrong size");
        else
            P::setP(demangledName, cache->vmAddrForContent((void*)newName));
    }

    void addPointers(ContentAccessor* cache, CacheBuilder::ASLR_Tracker& aslrTracker)
    {
        aslrTracker.add(&isa);
        aslrTracker.add(&name);
        if (protocols)               aslrTracker.add(&protocols);
        if (instanceMethods)         aslrTracker.add(&instanceMethods);
        if (classMethods)            aslrTracker.add(&classMethods);
        if (optionalInstanceMethods) aslrTracker.add(&optionalInstanceMethods);
        if (optionalClassMethods)    aslrTracker.add(&optionalClassMethods);
        if (instanceProperties)      aslrTracker.add(&instanceProperties);
        if (extendedMethodTypes)     aslrTracker.add(&extendedMethodTypes);
        if (demangledName)           aslrTracker.add(&demangledName);
        if (classProperties)         aslrTracker.add(&classProperties);
    }
};


template <typename P>
class objc_protocol_list_t {
    typedef typename P::uint_t pint_t;
    pint_t count;
    pint_t list[0];

    void* operator new (size_t, void* buf) { return buf; }

public:

    pint_t getCount() const { return (pint_t)P::getP(count); }

    pint_t getVMAddress(pint_t i) {
        return (pint_t)P::getP(list[i]);
    }

    objc_protocol_t<P>* get(ContentAccessor* cache, pint_t i) {
        return (objc_protocol_t<P>*)cache->contentForVMAddr(getVMAddress(i));
    }

    void setVMAddress(pint_t i, pint_t protoVMAddr) {
        P::setP(list[i], protoVMAddr);
    }
    
    void set(ContentAccessor* cache, pint_t i, objc_protocol_t<P>* proto) {
        setVMAddress(i, cache->vmAddrForContent(proto));
    }

    uint32_t byteSize() const {
        return byteSizeForCount(getCount()); 
    }
    static uint32_t byteSizeForCount(pint_t c) { 
        return sizeof(objc_protocol_list_t<P>) + c*sizeof(pint_t);
    }

    void getPointers(std::set<void*>& pointersToRemove) {
        for(int i=0 ; i < count; ++i) {
            pointersToRemove.insert(&list[i]);
        }
    }
#if 0
     static void addPointers(uint8_t* protocolList, CacheBuilder::ASLR_Tracker& aslrTracker) {
        objc_protocol_list_t<P>* plist = (objc_protocol_list_t<P>*)protocolList;
        for(int i=0 ; i < plist->count; ++i) {
            aslrTracker.add(&plist->list[i]);
        }
    }
#endif
    static objc_protocol_list_t<P>* newProtocolList(pint_t newCount) {
        void *buf = ::calloc(byteSizeForCount(newCount), 1);
        return new (buf) objc_protocol_list_t<P>(newCount);
    }

    void operator delete(void * p) { 
        ::free(p); 
    }

    objc_protocol_list_t(uint32_t newCount) : count(newCount) { }
private:
    // use newProtocolList instead
    void* operator new (size_t);
};


template <typename P>
class objc_class_data_t {
    typedef typename P::uint_t pint_t;
    uint32_t flags;
    uint32_t instanceStart;
    // Note there is 4-bytes of alignment padding between instanceSize and ivarLayout
    // on 64-bit archs, but no padding on 32-bit archs.
    // This union is a way to model that.
    union {
        uint32_t                instanceSize;
        pint_t   pad;
    } instanceSize;
    pint_t ivarLayout;
    pint_t name;
    pint_t baseMethods;
    pint_t baseProtocols;
    pint_t ivars;
    pint_t weakIvarLayout;
    pint_t baseProperties;

public:
    bool isMetaClass() { return P::E::get32(flags) & (1 << 0); }
    bool isRootClass() { return P::E::get32(flags) & (1 << 1); }

    uint32_t getInstanceStart() { return P::E::get32(instanceStart); }
    void setInstanceStart(uint32_t newStart) { P::E::set32(instanceStart, newStart); }
    
    uint32_t getInstanceSize() { return P::E::get32(instanceSize.instanceSize); }
    void setInstanceSize(uint32_t newSiz) { P::E::set32(instanceSize.instanceSize, newSiz); }

    objc_method_list_t<P> *getMethodList(ContentAccessor* cache) const { return (objc_method_list_t<P> *)cache->contentForVMAddr(P::getP(baseMethods)); }

    objc_protocol_list_t<P> *getProtocolList(ContentAccessor* cache) const { return (objc_protocol_list_t<P> *)cache->contentForVMAddr(P::getP(baseProtocols)); }

    objc_ivar_list_t<P> *getIvarList(ContentAccessor* cache) const { return (objc_ivar_list_t<P> *)cache->contentForVMAddr(P::getP(ivars)); }
    
    objc_property_list_t<P> *getPropertyList(ContentAccessor* cache) const { return (objc_property_list_t<P> *)cache->contentForVMAddr(P::getP(baseProperties)); }

    const char * getName(ContentAccessor* cache) const { return (const char *)cache->contentForVMAddr(P::getP(name)); }

    void setMethodList(ContentAccessor* cache, objc_method_list_t<P>* mlist) {
        P::setP(baseMethods, cache->vmAddrForContent(mlist));
    }

    void setProtocolList(ContentAccessor* cache, objc_protocol_list_t<P>* protolist) {
        P::setP(baseProtocols, cache->vmAddrForContent(protolist));
    }
 
    void setPropertyList(ContentAccessor* cache, objc_property_list_t<P>* proplist) {
        P::setP(baseProperties, cache->vmAddrForContent(proplist));
    }
    
    void addMethodListPointer(CacheBuilder::ASLR_Tracker& aslrTracker) {
        aslrTracker.add(&this->baseMethods);
    }
    
    void addPropertyListPointer(CacheBuilder::ASLR_Tracker& aslrTracker) {
        aslrTracker.add(&this->baseProperties);
    }
    
    void addProtocolListPointer(CacheBuilder::ASLR_Tracker& aslrTracker) {
        aslrTracker.add(&this->baseProtocols);
    }
};

template <typename P>
class objc_class_t {
    typedef typename P::uint_t pint_t;

    pint_t isa;
    pint_t superclass;
    pint_t method_cache;
    pint_t vtable;
    pint_t data;

public:
    bool isMetaClass(ContentAccessor* cache) const { return getData(cache)->isMetaClass(); }
    bool isRootClass(ContentAccessor* cache) const { return getData(cache)->isRootClass(); }

    objc_class_t<P> *getIsa(ContentAccessor* cache) const { return (objc_class_t<P> *)cache->contentForVMAddr(P::getP(isa)); }

    objc_class_t<P> *getSuperclass(ContentAccessor* cache) const { return (objc_class_t<P> *)cache->contentForVMAddr(P::getP(superclass)); }
    const pint_t* getSuperClassAddress() const { return &superclass; }

    // Low bit marks Swift classes.
    objc_class_data_t<P> *getData(ContentAccessor* cache) const { return (objc_class_data_t<P> *)cache->contentForVMAddr(P::getP(data & ~0x3LL)); }

    objc_class_t<P> *getVTable(ContentAccessor* cache) const { return (objc_class_t<P> *)cache->contentForVMAddr(P::getP(vtable)); }

    pint_t* getVTableAddress() { return &vtable; }

    objc_method_list_t<P> *getMethodList(ContentAccessor* cache) const {
        objc_class_data_t<P>* d = getData(cache);
        return d->getMethodList(cache);
    }

    objc_protocol_list_t<P> *getProtocolList(ContentAccessor* cache) const { return getData(cache)->getProtocolList(cache); }

    objc_property_list_t<P> *getPropertyList(ContentAccessor* cache) const { return getData(cache)->getPropertyList(cache); }

    const char* getName(ContentAccessor* cache) const {
        return getData(cache)->getName(cache);
    }

    void setMethodList(ContentAccessor* cache, objc_method_list_t<P>* mlist) {
        getData(cache)->setMethodList(cache, mlist);
    }

    void setProtocolList(ContentAccessor* cache, objc_protocol_list_t<P>* protolist) {
        getData(cache)->setProtocolList(cache, protolist);
    }

    void setPropertyList(ContentAccessor* cache, objc_property_list_t<P>* proplist) {
        getData(cache)->setPropertyList(cache, proplist);
    }
    
    void addMethodListPointer(ContentAccessor* cache, CacheBuilder::ASLR_Tracker& aslrTracker) {
        getData(cache)->addMethodListPointer(aslrTracker);
    }
    
    void addPropertyListPointer(ContentAccessor* cache, CacheBuilder::ASLR_Tracker& aslrTracker) {
        getData(cache)->addPropertyListPointer(aslrTracker);
    }
    
    void addProtocolListPointer(ContentAccessor* cache, CacheBuilder::ASLR_Tracker& aslrTracker) {
        getData(cache)->addProtocolListPointer(aslrTracker);
    }
    
};



template <typename P>
class objc_category_t {
    typedef typename P::uint_t pint_t;

    pint_t name;
    pint_t cls;
    pint_t instanceMethods;
    pint_t classMethods;
    pint_t protocols;
    pint_t instanceProperties;

public:

    const char * getName(ContentAccessor* cache) const { return (const char *)cache->contentForVMAddr(P::getP(name)); }

    objc_class_t<P> *getClass(ContentAccessor* cache) const { return (objc_class_t<P> *)cache->contentForVMAddr(P::getP(cls)); }

    objc_method_list_t<P> *getInstanceMethods(ContentAccessor* cache) const { return (objc_method_list_t<P> *)cache->contentForVMAddr(P::getP(instanceMethods)); }

    objc_method_list_t<P> *getClassMethods(ContentAccessor* cache) const { return (objc_method_list_t<P> *)cache->contentForVMAddr(P::getP(classMethods)); }

    objc_protocol_list_t<P> *getProtocols(ContentAccessor* cache) const { return (objc_protocol_list_t<P> *)cache->contentForVMAddr(P::getP(protocols)); }
 
    objc_property_list_t<P> *getInstanceProperties(ContentAccessor* cache) const { return (objc_property_list_t<P> *)cache->contentForVMAddr(P::getP(instanceProperties)); }

    void getPointers(std::set<void*>& pointersToRemove) {
        pointersToRemove.insert(&name);
        pointersToRemove.insert(&cls);
        pointersToRemove.insert(&instanceMethods);
        pointersToRemove.insert(&classMethods);
        pointersToRemove.insert(&protocols);
        pointersToRemove.insert(&instanceProperties);
    }


};

template <typename P>
class objc_message_ref_t {
    typedef typename P::uint_t pint_t;

    pint_t imp;
    pint_t sel;

public:
    pint_t getName() const { return (pint_t)P::getP(sel); }

    void setName(pint_t newName) { P::setP(sel, newName); }
};

// Call visitor.visitIvar() on every ivar in a given class.
template <typename P, typename V>
class IvarWalker {
    typedef typename P::uint_t pint_t;
    V& ivarVisitor;
public:
    
    IvarWalker(V& visitor) : ivarVisitor(visitor) { }
    
    void walk(ContentAccessor* cache, const macho_header<P>* header, objc_class_t<P> *cls)
    {
        objc_class_data_t<P> *data = cls->getData(cache);
        objc_ivar_list_t<P> *ivars = data->getIvarList(cache);
        if (ivars) {
            for (pint_t i = 0; i < ivars->getCount(); i++) {
                objc_ivar_t<P>* ivar = (objc_ivar_t<P>*)ivars->get(i);
                //fprintf(stderr, "visiting ivar: %s\n", ivar.getName(cache));
                ivarVisitor.visitIvar(cache, header, cls, ivar);
            }
        } else {
            //fprintf(stderr, "no ivars\n");
        }
    }
    
    void visitClass(ContentAccessor* cache, const macho_header<P>* header, objc_class_t<P> *cls)
    {
        walk(cache, header, cls);
    }
};

enum class ClassWalkerMode {
    ClassesOnly,
    ClassAndMetaclasses,
};

// Call visitor.visitClass() on every class.
template <typename P, typename V>
class ClassWalker {
    typedef typename P::uint_t pint_t;
    V& _visitor;
    ClassWalkerMode _mode;
public:
    
    ClassWalker(V& visitor, ClassWalkerMode mode = ClassWalkerMode::ClassesOnly) : _visitor(visitor), _mode(mode) { }
    
    void walk(ContentAccessor* cache, const macho_header<P>* header)
    {   
        PointerSection<P, objc_class_t<P>*> classList(cache, header, "__DATA", "__objc_classlist");
        
        for (pint_t i = 0; i < classList.count(); i++) {
            objc_class_t<P>* cls = classList.get(i);
            if (cls) {
                //fprintf(stderr, "visiting class: %s\n", cls->getName(cache));
                _visitor.visitClass(cache, header, cls);
                if (_mode == ClassWalkerMode::ClassAndMetaclasses) {
                    //fprintf(stderr, "visiting metaclass: %s\n", cls->getIsa(cache)->getName(cache));
                    _visitor.visitClass(cache, header, cls->getIsa(cache));
                }
            }
        }
    }
};

// Call visitor.visitProtocol() on every protocol.
template <typename P, typename V>
class ProtocolWalker {
    typedef typename P::uint_t pint_t;
    V& _protocolVisitor;
public:
    
    ProtocolWalker(V& visitor) : _protocolVisitor(visitor) { }
    
    void walk(ContentAccessor* cache, const macho_header<P>* header)
    {   
        PointerSection<P, objc_protocol_t<P> *>
            protocols(cache, header, "__DATA", "__objc_protolist");
        
        for (pint_t i = 0; i < protocols.count(); i++) {
            objc_protocol_t<P> *proto = protocols.get(i);
            _protocolVisitor.visitProtocol(cache, header, proto);
        }
    }
};

// Call visitor.visitProtocolReference() on every protocol.
template <typename P, typename V>
class ProtocolReferenceWalker {
    typedef typename P::uint_t pint_t;
    V& _visitor;

    void visitProtocolList(ContentAccessor* cache,
                           objc_protocol_list_t<P>* protolist)
    {
        if (!protolist) return;
        for (pint_t i = 0; i < protolist->getCount(); i++) {
            pint_t oldValue = protolist->getVMAddress(i);
            pint_t newValue = _visitor.visitProtocolReference(cache, oldValue);
            protolist->setVMAddress(i, newValue);
        }
    }

    friend class ClassWalker<P, ProtocolReferenceWalker<P, V>>;

    void visitClass(ContentAccessor* cache, const macho_header<P>*,
                    objc_class_t<P>* cls)
    {
        visitProtocolList(cache, cls->getProtocolList(cache));
        visitProtocolList(cache, cls->getIsa(cache)->getProtocolList(cache));
    }

public:
    
    ProtocolReferenceWalker(V& visitor) : _visitor(visitor) { }
    void walk(ContentAccessor* cache, const macho_header<P>* header)
    {
        // @protocol expressions
        PointerSection<P, objc_protocol_t<P> *>
            protorefs(cache, header, "__DATA", "__objc_protorefs");
        for (pint_t i = 0; i < protorefs.count(); i++) {
            pint_t oldValue = protorefs.getVMAddress(i);
            pint_t newValue = _visitor.visitProtocolReference(cache, oldValue);
            protorefs.setVMAddress(i, newValue);
        }

        // protocol lists in classes
        ClassWalker<P, ProtocolReferenceWalker<P, V>> classes(*this);
        classes.walk(cache, header);

        // protocol lists from categories
        PointerSection<P, objc_category_t<P> *>
        cats(cache, header, "__DATA", "__objc_catlist");
        for (pint_t i = 0; i < cats.count(); i++) {
            objc_category_t<P> *cat = cats.get(i);
            visitProtocolList(cache, cat->getProtocols(cache));
        }

        // protocol lists in protocols
        // __objc_protolists itself is NOT updated
        PointerSection<P, objc_protocol_t<P> *>
            protocols(cache, header, "__DATA", "__objc_protolist");
        for (pint_t i = 0; i < protocols.count(); i++) {
            objc_protocol_t<P>* proto = protocols.get(i);
            visitProtocolList(cache, proto->getProtocols(cache));
            // not recursive: every old protocol object 
            // must be in some protolist section somewhere
        }
    }
};

// Call visitor.visitMethodList(mlist) on every
// class and category method list in a header.
// Call visitor.visitProtocolMethodList(mlist, typelist) on every
// protocol method list in a header.
template <typename P, typename V>
class MethodListWalker {

    typedef typename P::uint_t pint_t;

    V& mVisitor;

public: 
    
    MethodListWalker(V& visitor) : mVisitor(visitor) { }

    void walk(ContentAccessor* cache, const macho_header<P>* header)
    {   
        // Method lists in classes
        PointerSection<P, objc_class_t<P> *> 
            classes(cache, header, "__DATA", "__objc_classlist");
            
        for (pint_t i = 0; i < classes.count(); i++) {
            objc_class_t<P> *cls = classes.get(i);
            objc_method_list_t<P> *mlist;
            if ((mlist = cls->getMethodList(cache))) {
                mVisitor.visitMethodList(cache, mlist);
            }
            if ((mlist = cls->getIsa(cache)->getMethodList(cache))) {
                mVisitor.visitMethodList(cache, mlist);
            }
        }
        
        // Method lists from categories
        PointerSection<P, objc_category_t<P> *> 
            cats(cache, header, "__DATA", "__objc_catlist");
        for (pint_t i = 0; i < cats.count(); i++) {
            objc_category_t<P> *cat = cats.get(i);
            objc_method_list_t<P> *mlist;
            if ((mlist = cat->getInstanceMethods(cache))) {
                mVisitor.visitMethodList(cache, mlist);
            }
            if ((mlist = cat->getClassMethods(cache))) {
                mVisitor.visitMethodList(cache, mlist);
            }
        }

        // Method description lists from protocols
        PointerSection<P, objc_protocol_t<P> *>
            protocols(cache, header, "__DATA", "__objc_protolist");
        for (pint_t i = 0; i < protocols.count(); i++) {
            objc_protocol_t<P> *proto = protocols.get(i);
            objc_method_list_t<P> *mlist;
            pint_t *typelist = proto->getExtendedMethodTypes(cache);

            if ((mlist = proto->getInstanceMethods(cache))) {
                mVisitor.visitProtocolMethodList(cache, mlist, typelist);
                if (typelist) typelist += mlist->getCount();
            }
            if ((mlist = proto->getClassMethods(cache))) {
                mVisitor.visitProtocolMethodList(cache, mlist, typelist);
                if (typelist) typelist += mlist->getCount();
            }
            if ((mlist = proto->getOptionalInstanceMethods(cache))) {
                mVisitor.visitProtocolMethodList(cache, mlist, typelist);
                if (typelist) typelist += mlist->getCount();
            }
            if ((mlist = proto->getOptionalClassMethods(cache))) {
                mVisitor.visitProtocolMethodList(cache, mlist, typelist);
                if (typelist) typelist += mlist->getCount();
            }
        }
    }
};

// Update selector references. The visitor performs recording and uniquing.
template <typename P, typename V>
class SelectorOptimizer {

    typedef typename P::uint_t pint_t;

    V& mVisitor;

    std::set<pint_t> selectorRefVMAddrs;

    // All relative method lists are offsets from this base address
    uint64_t _selectorBaseAddress = 0;

    friend class MethodListWalker<P, SelectorOptimizer<P,V> >;
    void visitMethodList(ContentAccessor* cache, objc_method_list_t<P> *mlist)
    {
        assert(_selectorBaseAddress != 0);
        // Gather selectors. Update method names.
        for (uint32_t m = 0; m < mlist->getCount(); m++) {
            // Read names as relative offsets to selRefs
            pint_t oldValue = mlist->getName(cache, m, {});
            pint_t newValue = mVisitor.visit(oldValue);
            // And write names as relative offsets to SELs themselves.
            mlist->setName(cache, m, newValue, _selectorBaseAddress);
        }
        // Set this method list as now being relative offsets directly to the selector string
        if ( mlist->usesRelativeMethods() )
            mlist->setMethodListSelectorsAreDirect();

        // Do not setFixedUp: the methods are not yet sorted.
    }

    void visitProtocolMethodList(ContentAccessor* cache, objc_method_list_t<P> *mlist, pint_t *types)
    {
        visitMethodList(cache, mlist);
    }

public:

    SelectorOptimizer(V& visitor) : mVisitor(visitor) {
    }

    const objc::string_map& strings() const {
        return mVisitor.strings();
    }

    void visitCoalescedStrings(const CacheBuilder::CoalescedStringsSection& coalescedMethodNames) {
        mVisitor.visitCoalescedStrings(coalescedMethodNames);
    }

    void setSelectorBaseAddress(uint64_t selectorBaseAddress) {
        _selectorBaseAddress = selectorBaseAddress;
    }

    void optimize(ContentAccessor* cache, const macho_header<P>* header)
    {
        // method lists in classes, categories, and protocols
        MethodListWalker<P, SelectorOptimizer<P,V> > mw(*this);
        mw.walk(cache, header);
        
        // @selector references
        PointerSection<P, const char *> 
            selrefs(cache, header, "__DATA", "__objc_selrefs");
        for (pint_t i = 0; i < selrefs.count(); i++) {
            pint_t oldValue = selrefs.getVMAddress(i);
            pint_t newValue = mVisitor.visit(oldValue);
            selrefs.setVMAddress(i, newValue);
            selectorRefVMAddrs.insert(selrefs.getSectionVMAddress() + (i * sizeof(pint_t)));
        }

        // message references
        ArraySection<P, objc_message_ref_t<P> > 
            msgrefs(cache, header, "__DATA", "__objc_msgrefs");
        for (pint_t i = 0; i < msgrefs.count(); i++) {
            objc_message_ref_t<P>& msg = msgrefs.get(i);
            pint_t oldValue = msg.getName();
            pint_t newValue = mVisitor.visit(oldValue);
            msg.setName(newValue);
        }
    }

    bool isSelectorRefAddress(pint_t vmAddr) const {
        return selectorRefVMAddrs.count(vmAddr);
    }
};


// Update selector references. The visitor performs recording and uniquing.
template <typename P>
class IvarOffsetOptimizer {
    uint32_t    _slide;
    uint32_t    _maxAlignment;
    uint32_t    _optimized;

public:
    
    IvarOffsetOptimizer() : _optimized(0) { }

    size_t optimized() const { return _optimized; }
    
    // dual purpose ivar visitor function
    // if slide!=0 then slides the ivar by that amount, otherwise computes _maxAlignment
    void visitIvar(ContentAccessor* cache, const macho_header<P>* /*unused, may be NULL*/, objc_class_t<P> *cls, objc_ivar_t<P> *ivar)
    {
        if (_slide == 0) {
            uint32_t alignment = ivar->getAlignment();
            if (alignment > _maxAlignment) _maxAlignment = alignment;
        } else {
            // skip anonymous bitfields
            if (ivar->hasOffset()) {
                uint32_t oldOffset = (uint32_t)ivar->getOffset(cache);
                ivar->setOffset(cache, oldOffset + _slide);
                _optimized++;
                //fprintf(stderr, "%d -> %d for %s.%s\n", oldOffset, oldOffset + _slide, cls->getName(cache), ivar->getName(cache));
            } else {
                //fprintf(stderr, "NULL offset\n");
            }
        }
    }
    
    // Class visitor function. Evaluates whether to slide ivars and performs slide if needed.
    // The slide algorithm is also implemented in objc. Any changes here should be reflected there also.
    void visitClass(ContentAccessor* cache, const macho_header<P>* /*unused, may be NULL*/, objc_class_t<P> *cls)
    {
        objc_class_t<P> *super = cls->getSuperclass(cache);
        if (super) {
            // Recursively visit superclasses to ensure we have the correct superclass start
            // Note that we don't need the macho_header, so just pass NULL.
            visitClass(cache, nullptr, super);

            objc_class_data_t<P> *data = cls->getData(cache);
            objc_class_data_t<P> *super_data = super->getData(cache);
            int32_t diff = super_data->getInstanceSize() - data->getInstanceStart();
            if (diff > 0) {
                IvarWalker<P, IvarOffsetOptimizer<P> > ivarVisitor(*this);
                _maxAlignment = 1;
                _slide = 0;
                
                // This walk computes _maxAlignment
                ivarVisitor.walk(cache, nullptr, cls);

                // Compute a slide value that preserves that alignment
                uint32_t alignMask = _maxAlignment - 1;
                if (diff & alignMask) diff = (diff + alignMask) & ~alignMask;

                // Slide all of this class's ivars en masse
                _slide = diff;
                if (_slide != 0) {
                    //fprintf(stderr, "Sliding ivars in %s by %u (superclass was %d, now %d)\n", cls->getName(cache), _slide, data->getInstanceStart(), super_data->getInstanceSize());
                    ivarVisitor.walk(cache, nullptr, cls);
                    data->setInstanceStart(data->getInstanceStart() + _slide);
                    data->setInstanceSize(data->getInstanceSize() + _slide);
                }
            }
        }
    }
    
    // Enumerates objc classes in the module and performs any ivar slides
    void optimize(ContentAccessor* cache, const macho_header<P>* header)
    {
        // The slide code cannot fix up GC layout strings so skip modules that support or require GC
        const macho_section<P> *imageInfoSection = header->getSection("__DATA", "__objc_imageinfo");
        if (imageInfoSection) {
            objc_image_info<P> *info = (objc_image_info<P> *)cache->contentForVMAddr(imageInfoSection->addr());
            if (!info->supportsGCFlagSet() && !info->requiresGCFlagSet()) {
                ClassWalker<P, IvarOffsetOptimizer<P> > classVisitor(*this);
                classVisitor.walk(cache, header);
            } else {
                //fprintf(stderr, "GC support present - skipped module\n");
            }
        }
    }
};


// Detect classes that have missing weak-import superclasses.
template <typename P>
class WeakClassDetector {
    bool                                noMissing;
    const std::map<void*, std::string>* missingWeakImports = nullptr;

    friend class ClassWalker<P, WeakClassDetector<P>>;
    void visitClass(ContentAccessor* cache, const macho_header<P>*, 
                    objc_class_t<P>* cls)
    {
        auto supercls = cls->getSuperclass(cache);
        if (supercls) {
            // okay: class with superclass
            // Note that the superclass itself might have a missing superclass.
            // That is fine for mere detection because we will visit the 
            // superclass separately.
        } else if (cls->isRootClass(cache)) {
            // okay: root class is expected to have no superclass
        } else {
            // bad: cls's superclass is missing.
            // See if we can find the name from the missing weak import map
            auto it = missingWeakImports->find((void*)cls->getSuperClassAddress());
            const char* dylibName = "unknown dylib";
            if (it != missingWeakImports->end()) {
                dylibName = it->second.c_str();
            }
            cache->diagnostics().warning("Superclass of class '%s' is weak-import and missing.  Expected in %s",
                                         cls->getName(cache), dylibName);
            noMissing = false;
        }
    }

public:
    bool noMissingWeakSuperclasses(ContentAccessor* cache,
                                   const std::map<void*, std::string>& missingWeakImportsMap,
                                   std::vector<const macho_header<P>*> dylibs)
    {
        noMissing           = true;
        missingWeakImports  = &missingWeakImportsMap;
        ClassWalker<P, WeakClassDetector<P>> classes(*this);
        for (auto mh : dylibs) {
            classes.walk(cache, mh);
        }
        return noMissing;
    }
};


// Sort methods in place by selector.
template <typename P>
class MethodListSorter {

    typedef typename P::uint_t pint_t;

    uint32_t                _optimized;
    std::optional<uint64_t> _isOffsetFromBase = {};

    friend class MethodListWalker<P, MethodListSorter<P> >;

    void sortMethodList(ContentAccessor* cache, objc_method_list_t<P> *mlist, pint_t *typelist) {
        mlist->sortMethods(cache, typelist, _isOffsetFromBase);
        _optimized++;
    }

    void visitMethodList(ContentAccessor* cache, objc_method_list_t<P> *mlist)
    {
        sortMethodList(cache, mlist, nullptr);
    }

    void visitProtocolMethodList(ContentAccessor* cache, objc_method_list_t<P> *mlist, pint_t *typelist)
    {
        sortMethodList(cache, mlist, typelist);
    }

public:
    MethodListSorter(std::optional<uint64_t> isOffsetFromBase) : _optimized(0), _isOffsetFromBase(isOffsetFromBase) { }

    size_t optimized() const { return _optimized; }

    void optimize(ContentAccessor* cache, const macho_header<P>* header)
    {
        MethodListWalker<P, MethodListSorter<P> > mw(*this);
        mw.walk(cache, header);
    }
};


template <typename P, typename InfoT>
class HeaderInfoOptimizer {
public:

    typedef typename P::uint_t pint_t;

    HeaderInfoOptimizer() : _hInfos(0), _count(0) { }

    const char* init(uint32_t count, uint8_t*& buf, size_t& bufSize) {
        if (count == 0)
            return nullptr;

        size_t requiredSize = 
            2*sizeof(uint32_t) + count*sizeof(InfoT);
        if (bufSize < requiredSize) {
            return "libobjc's read/write section is too small (metadata not optimized)";
        }

        uint32_t *buf32 = (uint32_t *)buf;
        P::E::set32(buf32[0], count);
        P::E::set32(buf32[1], sizeof(InfoT));
        _hInfos = (InfoT*)(buf32+2);

        buf += requiredSize;
        bufSize -= requiredSize;

        return nullptr;
    }

    void update(ContentAccessor* cache, const macho_header<P>* mh) {
        InfoT* hi = new(&_hInfos[_count++]) InfoT(cache, mh);
        (void)hi;
    }

    InfoT* hinfoForHeader(ContentAccessor* cache, const macho_header<P>* mh) {
        // FIXME: could be binary search
        uint64_t mh_vmaddr = cache->vmAddrForContent((void*)mh);
        for (size_t i = 0; i < _count; i++) {
            InfoT* hi = &_hInfos[i];
            if (hi->header_vmaddr(cache) == mh_vmaddr) return hi;
        }
        return nullptr;
    }
private:
    InfoT*                    _hInfos;
    size_t                    _count;
};
