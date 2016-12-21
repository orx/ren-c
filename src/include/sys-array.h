//
//  File: %sys-array.h
//  Summary: {Definitions for REBARR}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A "Rebol Array" is a series of REBVAL values which is terminated by an
// END marker.  In R3-Alpha, the END marker was itself a full-sized REBVAL
// cell...so code was allowed to write one cell past the capacity requested
// when Make_Array() was called.  But this always had to be an END.
//
// In Ren-C, there is an implicit END marker just past the last cell in the
// capacity.  Allowing a SET_END() on this position could corrupt the END
// signaling slot, which only has to have its low bit zero (vs. be the size of
// an entire cell).  Use TERM_ARRAY_LEN() to safely terminate arrays and
// respect not writing if it's past capacity.
//
// While many operations are shared in common with REBSER, there is a
// (deliberate) type incompatibility introduced.  The type compatibility is
// implemented in a way that works in C or C++ (though it should be reviewed
// for strict aliasing compliance).  To get the underlying REBSER of a REBARR
// use the AS_SERIES() operation.
//
// An ARRAY is the main place in the system where "relative" values come
// from, because all relative words are created during the copy of the
// bodies of functions.  The array accessors must err on the safe side and
// give back a relative value.  Many inspection operations are legal on
// a relative value, but it cannot be copied without a "specifier" FRAME!
// context (which is also required to do a GET_VAR lookup).
//

struct Reb_Array {
    struct Reb_Series series; // http://stackoverflow.com/a/9747062
};

// These do REBSER <=> REBARR coercion.  Although it's desirable to make
// them type incompatible for most purposes, some operations require treating
// one kind of pointer as the other (and they are both Reb_Series)
//
#if defined(__cplusplus) && __cplusplus >= 201103L
    #include <type_traits>

    template <class T>
    inline REBARR *AS_ARRAY(T *p) {
        static_assert(
            std::is_same<T, void>::value
            || std::is_same<T, REBNOD>::value
            || std::is_same<T, REBSER>::value,
            "AS_ARRAY works on: void*, REBNOD*, REBSER*"
        );
        REBSER *s = cast(REBSER*, p);
        assert(GET_SER_FLAG(s, SERIES_FLAG_ARRAY));
        return cast(REBARR*, s);
    }
#else
    #define AS_ARRAY(p) \
        cast(REBARR*, (p))
#endif


// HEAD, TAIL, and LAST refer to specific value pointers in the array.  An
// empty array should have an END marker in its head slot, and since it has
// no last value then ARR_LAST should not be called (this is checked in
// debug builds).  A fully constructed array should always have an END
// marker in its tail slot, which is one past the last position that is
// valid for writing a full REBVAL.

#define ARR_AT(a, n) \
    SER_AT(RELVAL, AS_SERIES(a), (n))

#define ARR_HEAD(a) \
    SER_HEAD(RELVAL, AS_SERIES(a))

#define ARR_TAIL(a) \
    SER_TAIL(RELVAL, AS_SERIES(a))

#define ARR_LAST(a) \
    SER_LAST(RELVAL, AS_SERIES(a))

// As with an ordinary REBSER, a REBARR has separate management of its length
// and its terminator.  Many routines seek to control these independently for
// performance reasons (for better or worse).
//
inline static REBCNT ARR_LEN(REBARR *a) {
    assert(Is_Array_Series(AS_SERIES(a)));
    return SER_LEN(AS_SERIES(a));
}


// TERM_ARRAY_LEN sets the length and terminates the array, and to get around
// the problem it checks to see if the length is the rest - 1.  Another
// possibility would be to check to see if the cell was already marked with
// END...however, that would require initialization of all cells in an array
// up front, to legitimately examine the bits (and decisions on how to init)
//
inline static void TERM_ARRAY_LEN(REBARR *a, REBCNT len) {
    REBCNT rest = SER_REST(AS_SERIES(a));
    assert(len < rest);
    SET_SERIES_LEN(AS_SERIES(a), len);
    if (len + 1 == rest)
        assert(IS_END(ARR_TAIL(a)));
    else
        SET_END(ARR_TAIL(a));
}

inline static void SET_ARRAY_LEN_NOTERM(REBARR *a, REBCNT len) {
    SET_SERIES_LEN(AS_SERIES(a), len); // call out non-terminating usages
}

inline static void RESET_ARRAY(REBARR *a) {
    TERM_ARRAY_LEN(a, 0);
}

inline static void TERM_SERIES(REBSER *s) {
    if (Is_Array_Series(s))
        TERM_ARRAY_LEN(AS_ARRAY(s), SER_LEN(s));
    else
        memset(SER_AT_RAW(SER_WIDE(s), s, SER_LEN(s)), 0, SER_WIDE(s));
}


// Setting and getting array flags is common enough to want a macro for it
// vs. having to extract the ARR_SERIES to do it each time.
//
#define FAIL_IF_LOCKED_ARRAY(a) \
    FAIL_IF_LOCKED_SERIES(AS_SERIES(a))

#define IS_ARRAY_MANAGED(a) \
    IS_SERIES_MANAGED(AS_SERIES(a))

#define MANAGE_ARRAY(a) \
    MANAGE_SERIES(AS_SERIES(a))

#define ENSURE_ARRAY_MANAGED(a) \
    ENSURE_SERIES_MANAGED(AS_SERIES(a))

#define PUSH_GUARD_ARRAY(a) \
    PUSH_GUARD_SERIES(AS_SERIES(a))

#define DROP_GUARD_ARRAY(a) \
    DROP_GUARD_SERIES(AS_SERIES(a))

inline static void PUSH_GUARD_ARRAY_CONTENTS(REBARR *a) {
    assert(!IS_ARRAY_MANAGED(a)); // if managed, just use PUSH_GUARD_ARRAY
    Guard_Node_Core(cast(REBNOD*, a));
}

inline static void DROP_GUARD_ARRAY_CONTENTS(REBARR *a) {
#if !defined(NDEBUG)
    //
    // Make sure no unmanaged values were put in the array, because they
    // would have caused errors if the GC had seen them!
    //
    RELVAL *test = ARR_HEAD(a);
    for (; NOT_END(test); ++test)
        ASSERT_VALUE_MANAGED(test);
#endif
    DROP_GUARD_SERIES(AS_SERIES(a));
}


// Make a series that is the right size to store REBVALs (and
// marked for the garbage collector to look into recursively).
// Terminator included implicitly. Sets TAIL to zero.
//
inline static REBARR *Make_Array(REBCNT capacity)
{
    REBSER *s = Make_Series(capacity + 1, sizeof(REBVAL), MKS_ARRAY);
    assert(
        capacity <= 1
            ? NOT(GET_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC))
            : GET_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC)
    );

    REBARR *a = AS_ARRAY(s);
    TERM_ARRAY_LEN(a, 0);
    return a;
}


// A singular array is specifically optimized to hold *one* value in a REBSER
// directly, and stay fixed at that size.  Note that the internal logic of
// series will give you this optimization even if you don't ask for it if
// a series or array is small.  However, this allocator adds the fixed size
// bit and defaults the array to an uninitialized cell with length 1, vs.
// going through a length 0 step.
//
inline static REBARR *Alloc_Singular_Array(void) {
    REBSER *s = Make_Series(2, sizeof(REBVAL), MKS_ARRAY); // no real 2nd slot
    assert(NOT(GET_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC)));

    REBARR *a = AS_ARRAY(s);
    SET_SER_FLAG(a, SERIES_FLAG_FIXED_SIZE);

    SET_SERIES_LEN(s, 1); // currently needs length bits set
    assert(IS_END(ARR_TAIL(a)));

    return a;
}


#define Append_Value(a,v) \
    (*Alloc_Tail_Array(a) = *(v), NOOP)

#define Append_Value_Core(a,v,s) \
    Derelativize(Alloc_Tail_Array(a), (v), (s))


#define Copy_Values_Len_Shallow(v,s,l) \
    Copy_Values_Len_Extra_Skip_Shallow((v), (s), (l), 0, 1)

#define Copy_Values_Len_Reversed_Shallow(v,s,l) \
    Copy_Values_Len_Extra_Skip_Shallow((v), (s), (l), 0, -1)

#define Copy_Values_Len_Extra_Shallow(v, s, l, e) \
    Copy_Values_Len_Extra_Skip_Shallow((v), (s), (l), (e), 1) 


#define Copy_Array_Shallow(a,s) \
    Copy_Array_At_Shallow((a), 0, (s))

#define Copy_Array_Deep_Managed(a,s) \
    Copy_Array_At_Extra_Deep_Managed((a), 0, (s), 0)

#define Copy_Array_At_Deep_Managed(a,i,s) \
    Copy_Array_At_Extra_Deep_Managed((a), (i), (s), 0)

#define COPY_ANY_ARRAY_AT_DEEP_MANAGED(v) \
    Copy_Array_At_Extra_Deep_Managed( \
        VAL_ARRAY(v), VAL_INDEX(v), VAL_SPECIFIER(v), 0)

#define Copy_Array_At_Shallow(a,i,s) \
    Copy_Array_At_Extra_Shallow((a), (i), (s), 0)

#define Copy_Array_Extra_Shallow(a,s,e) \
    Copy_Array_At_Extra_Shallow((a), 0, (s), (e))


#define Free_Array(a) \
    Free_Series(AS_SERIES(a))



//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-ARRAY! (uses `struct Reb_Any_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See INIT_SPECIFIC and INIT_RELATIVE in %sys-bind.h
//

#define EMPTY_BLOCK \
    ROOT_EMPTY_BLOCK

#define EMPTY_ARRAY \
    VAL_ARRAY(ROOT_EMPTY_BLOCK)

#define EMPTY_STRING \
    ROOT_EMPTY_STRING

inline static REBSPC* AS_SPECIFIER(REBCTX *context) {
    return cast(REBSPC*, context);
}

inline static REBSPC *VAL_SPECIFIER(const REBVAL *v) {
    assert(ANY_ARRAY(v));
    return AS_SPECIFIER(VAL_SPECIFIC(v));
}

inline static void INIT_VAL_ARRAY(RELVAL *v, REBARR *a) {
    v->extra.binding = (REBARR*)SPECIFIED; // !!! cast() complains, investigate
    v->payload.any_series.series = AS_SERIES(a);
}

// These array operations take the index position into account.  The use
// of the word AT with a missing index is a hint that the index is coming
// from the VAL_INDEX() of the value itself.
//
#define VAL_ARRAY_AT(v) \
    ARR_AT(VAL_ARRAY(v), VAL_INDEX(v))

#define VAL_ARRAY_LEN_AT(v) \
    VAL_LEN_AT(v)

// These operations do not need to take the value's index position into
// account; they strictly operate on the array series
//
inline static REBARR *VAL_ARRAY(const RELVAL *v) {
    assert(ANY_ARRAY(v));
    return AS_ARRAY(v->payload.any_series.series);
}

#define VAL_ARRAY_HEAD(v) \
    ARR_HEAD(VAL_ARRAY(v))

inline static RELVAL *VAL_ARRAY_TAIL(const RELVAL *v) {
    return ARR_AT(VAL_ARRAY(v), VAL_ARRAY_LEN_AT(v));
}


// !!! VAL_ARRAY_AT_HEAD() is a leftover from the old definition of
// VAL_ARRAY_AT().  Unlike SKIP in Rebol, this definition did *not* take
// the current index position of the value into account.  It rather extracted
// the array, counted rom the head, and disregarded the index entirely.
//
// The best thing to do with it is probably to rewrite the use cases to
// not need it.  But at least "AT HEAD" helps communicate what the equivalent
// operation in Rebol would be...and you know it's not just giving back the
// head because it's taking an index.  So  it looks weird enough to suggest
// looking here for what the story is.
//
#define VAL_ARRAY_AT_HEAD(v,n) \
    ARR_AT(VAL_ARRAY(v), (n))

#define Init_Any_Array_At(v,t,a,i) \
    Init_Any_Series_At((v), (t), AS_SERIES(a), (i))

#define Init_Any_Array(v,t,a) \
    Init_Any_Array_At((v), (t), (a), 0)

#define Init_Block(v,s) \
    Init_Any_Array((v), REB_BLOCK, (s))

#define Init_Group(v,s) \
    Init_Any_Array((v), REB_GROUP, (s))

#define Init_Path(v,s) \
    Init_Any_Array((v), REB_PATH, (s))



#ifdef NDEBUG
    #define ASSERT_ARRAY(s) \
        NOOP

    #define ASSERT_ARRAY_MANAGED(array) \
        NOOP

    #define ASSERT_SERIES(s) \
        NOOP
#else
    #define ASSERT_ARRAY(s) \
        Assert_Array_Core(s)

    #define ASSERT_ARRAY_MANAGED(array) \
        ASSERT_SERIES_MANAGED(AS_SERIES(array))

    static inline void ASSERT_SERIES(REBSER *s) {
        if (Is_Array_Series(s))
            Assert_Array_Core(AS_ARRAY(s));
        else
            Assert_Series_Core(s);
    }

    #define IS_VALUE_IN_ARRAY_DEBUG(a,v) \
        (ARR_LEN(a) != 0 && (v) >= ARR_HEAD(a) && (v) < ARR_TAIL(a))
#endif
