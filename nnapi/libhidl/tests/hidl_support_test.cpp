// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "hidl/HidlSupport.h"

// Unit tests for HidlSupport, taken from upstream. We do not want all
// of the upstream tests as we are not porting all of the classes from libhidl
// to work on Chrome OS, and upstream has all of the tests lumped into a single
// file src/aosp/system/libhidl/test_main.cpp.

#define EXPECT_ARRAYEQ(__a1__, __a2__, __size__) \
    EXPECT_TRUE(isArrayEqual(__a1__, __a2__, __size__))

#define EXPECT_2DARRAYEQ(__a1__, __a2__, __size1__, __size2__) \
        EXPECT_TRUE(is2dArrayEqual(__a1__, __a2__, __size1__, __size2__))

template<typename T, typename S>
static inline bool isArrayEqual(const T arr1, const S arr2, size_t size) {
    for (size_t i = 0; i < size; i++)
        if (arr1[i] != arr2[i])
            return false;
    return true;
}

template<typename T, typename S>
static inline bool is2dArrayEqual(const T arr1, const S arr2, size_t size1,
    size_t size2) {
    for (size_t i = 0; i < size1; i++)
        for (size_t j = 0; j < size2; j++)
            if (arr1[i][j] != arr2[i][j])
                return false;
    return true;
}

TEST(LibHidlTest, StringTest) {
    using android::hardware::hidl_string;
    hidl_string s;  // empty constructor
    EXPECT_STREQ(s.c_str(), "");
    hidl_string s1 = "s1";  // copy = from cstr
    EXPECT_STREQ(s1.c_str(), "s1");
    hidl_string s2("s2");  // copy constructor from cstr
    EXPECT_STREQ(s2.c_str(), "s2");
    hidl_string s2a(nullptr);  // copy constructor from null cstr
    EXPECT_STREQ("", s2a.c_str());
    s2a = nullptr;  // = from nullptr cstr
    EXPECT_STREQ(s2a.c_str(), "");
    hidl_string s3 = hidl_string("s3");  // move =
    EXPECT_STREQ(s3.c_str(), "s3");
    // copy constructor from cstr w/ length
    hidl_string s4 = hidl_string("12345", 3);
    EXPECT_STREQ(s4.c_str(), "123");
    hidl_string s5(hidl_string(hidl_string("s5")));  // move constructor
    EXPECT_STREQ(s5.c_str(), "s5");
    hidl_string s6(std::string("s6"));  // copy constructor from std::string
    EXPECT_STREQ(s6.c_str(), "s6");
    hidl_string s7 = std::string("s7");  // copy = from std::string
    EXPECT_STREQ(s7.c_str(), "s7");
    hidl_string s8(s7);  // copy constructor // NOLINT, test the copy ctor
    EXPECT_STREQ(s8.c_str(), "s7");
    hidl_string s9 = s8;  // copy =  // NOLINT, test the copy operator
    EXPECT_STREQ(s9.c_str(), "s7");
    char myCString[20] = "myCString";
    s.setToExternal(&myCString[0], strlen(myCString));
    EXPECT_STREQ(s.c_str(), "myCString");
    myCString[2] = 'D';
    EXPECT_STREQ(s.c_str(), "myDString");
    s.clear();  // should not affect myCString
    EXPECT_STREQ(myCString, "myDString");

    // casts
    s = "great";
    std::string myString = s;
    const char *anotherCString = s.c_str();
    EXPECT_EQ(myString, "great");
    EXPECT_STREQ(anotherCString, "great");

    const hidl_string t = "not so great";
    std::string myTString = t;
    const char * anotherTCString = t.c_str();
    EXPECT_EQ(myTString, "not so great");
    EXPECT_STREQ(anotherTCString, "not so great");

    // Assignment from hidl_string to std::string
    std::string tgt;
    hidl_string src("some stuff");
    tgt = src;
    EXPECT_STREQ(tgt.c_str(), "some stuff");

    // Stream output operator
    hidl_string msg("hidl_string works with operator<<");
    std::cout << msg;

    // Comparisons
    const char * cstr1 = "abc";
    std::string string1(cstr1);
    hidl_string hs1(cstr1);
    const char * cstrE = "abc";
    std::string stringE(cstrE);
    hidl_string hsE(cstrE);
    const char * cstrNE = "ABC";
    std::string stringNE(cstrNE);
    hidl_string hsNE(cstrNE);
    const char * cstr2 = "def";
    std::string string2(cstr2);
    hidl_string hs2(cstr2);

    EXPECT_TRUE(hs1  == hsE);
    EXPECT_FALSE(hs1 == hsNE);
    EXPECT_TRUE(hs1  == cstrE);
    EXPECT_FALSE(hs1 == cstrNE);
    EXPECT_TRUE(hs1  == stringE);
    EXPECT_FALSE(hs1 == stringNE);
    EXPECT_FALSE(hs1 != hsE);
    EXPECT_TRUE(hs1  != hsNE);
    EXPECT_FALSE(hs1 != cstrE);
    EXPECT_TRUE(hs1  != cstrNE);
    EXPECT_FALSE(hs1 != stringE);
    EXPECT_TRUE(hs1  != stringNE);

    EXPECT_TRUE(hs1 < hs2);
    EXPECT_FALSE(hs2 < hs1);
    EXPECT_TRUE(hs2 > hs1);
    EXPECT_FALSE(hs1 > hs2);
    EXPECT_TRUE(hs1 <= hs1);
    EXPECT_TRUE(hs1 <= hs2);
    EXPECT_FALSE(hs2 <= hs1);
    EXPECT_TRUE(hs1 >= hs1);
    EXPECT_TRUE(hs2 >= hs1);
    EXPECT_FALSE(hs2 <= hs1);
}

TEST(LibHidlTest, MemoryTest) {
    using android::hardware::hidl_memory;

    hidl_memory mem1 = hidl_memory();  // default constructor
    hidl_memory mem2 = mem1;  // copy constructor (nullptr), NOLINT

    EXPECT_EQ(nullptr, mem2.handle());

    native_handle_t* testHandle = native_handle_create(0, 0);

    hidl_memory mem3 = hidl_memory("foo", testHandle, 42);  // owns testHandle
    hidl_memory mem4 = mem3;  // copy constructor (regular handle), NOLINT

    EXPECT_EQ(mem3.name(), mem4.name());
    EXPECT_EQ(mem3.size(), mem4.size());
    EXPECT_NE(nullptr, mem4.handle());
    EXPECT_NE(mem3.handle(), mem4.handle());  // check handle cloned

    // hidl memory works with nullptr handle
    hidl_memory mem5 = hidl_memory("foo", nullptr, 0);
    hidl_memory mem6 = mem5;  // NOLINT, test copying
    EXPECT_EQ(nullptr, mem5.handle());
    EXPECT_EQ(nullptr, mem6.handle());
}

TEST(LibHidlTest, VecInitTest) {
    using android::hardware::hidl_vec;
    using std::vector;
    int32_t array[] = {5, 6, 7};
    vector<int32_t> v(array, array + 3);

    hidl_vec<int32_t> hv0(3);  // size
    EXPECT_EQ(hv0.size(), 3ul);  // cannot say anything about its contents

    hidl_vec<int32_t> hv1 = v;  // copy =
    EXPECT_ARRAYEQ(hv1, array, 3);
    EXPECT_ARRAYEQ(hv1, v, 3);
    hidl_vec<int32_t> hv2(v);  // copy constructor
    EXPECT_ARRAYEQ(hv2, v, 3);

    vector<int32_t> v2 = hv1;  // cast
    EXPECT_ARRAYEQ(v2, v, 3);

    hidl_vec<int32_t> v3 = {5, 6, 7};  // initializer_list
    EXPECT_EQ(v3.size(), 3ul);
    EXPECT_ARRAYEQ(v3, array, v3.size());
}

TEST(LibHidlTest, VecReleaseTest) {
    // this test indicates an inconsistency of behaviors which is undesirable.
    // Perhaps hidl-vec should always allocate an empty vector whenever it
    // exposes its data. Alternatively, perhaps it should always free/reject
    // empty vectors and always return nullptr for this state. While this second
    // alternative is faster, it makes client code harder to write, and it would
    // break existing client code.
    using android::hardware::hidl_vec;

    hidl_vec<int32_t> empty;
    EXPECT_EQ(nullptr, empty.releaseData());

    empty.resize(0);
    int32_t* data = empty.releaseData();
    EXPECT_NE(nullptr, data);
    delete data;
}

TEST(LibHidlTest, VecIterTest) {
    int32_t array[] = {5, 6, 7};
    android::hardware::hidl_vec<int32_t> hv1 =
        std::vector<int32_t>(array, array + 3);

    auto iter = hv1.begin();    // iterator begin()
    EXPECT_EQ(*iter++, 5);
    EXPECT_EQ(*iter, 6);
    EXPECT_EQ(*++iter, 7);
    EXPECT_EQ(*iter--, 7);
    EXPECT_EQ(*iter, 6);
    EXPECT_EQ(*--iter, 5);

    iter += 2;
    EXPECT_EQ(*iter, 7);
    iter -= 2;
    EXPECT_EQ(*iter, 5);

    iter++;
    EXPECT_EQ(*(iter + 1), 7);
    EXPECT_EQ(*(1 + iter), 7);
    EXPECT_EQ(*(iter - 1), 5);
    EXPECT_EQ(*iter, 6);

    auto five = iter - 1;
    auto seven = iter + 1;
    EXPECT_EQ(seven - five, 2);
    EXPECT_EQ(five - seven, -2);

    EXPECT_LT(five, seven);
    EXPECT_LE(five, seven);
    EXPECT_GT(seven, five);
    EXPECT_GE(seven, five);

    EXPECT_EQ(seven[0], 7);
    EXPECT_EQ(five[1], 6);
}

TEST(LibHidlTest, ArrayTest) {
    using android::hardware::hidl_array;
    int32_t array[] = {5, 6, 7};

    hidl_array<int32_t, 3> ha(array);
    EXPECT_ARRAYEQ(ha, array, 3);
}

TEST(LibHidlTest, StringCmpTest) {
    using android::hardware::hidl_string;
    const char * s = "good";
    hidl_string hs(s);
    EXPECT_NE(hs.c_str(), s);

    EXPECT_TRUE(hs == s);  // operator ==
    EXPECT_TRUE(s == hs);

    EXPECT_FALSE(hs != s);  // operator ==
    EXPECT_FALSE(s != hs);
}

template <typename T>
void great(android::hardware::hidl_vec<T>) {}

TEST(LibHidlTest, VecCopyTest) {
    android::hardware::hidl_vec<int32_t> v;
    great(v);
}

TEST(LibHidlTest, StdArrayTest) {
    using android::hardware::hidl_array;
    hidl_array<int32_t, 5> array{(int32_t[5]){1, 2, 3, 4, 5}};
    std::array<int32_t, 5> stdArray = array;
    EXPECT_ARRAYEQ(array.data(), stdArray.data(), 5);
    hidl_array<int32_t, 5> array2 = stdArray;
    EXPECT_ARRAYEQ(array.data(), array2.data(), 5);
}

TEST(LibHidlTest, MultiDimStdArrayTest) {
    using android::hardware::hidl_array;
    hidl_array<int32_t, 2, 3> array;
    for (size_t i = 0; i < 2; i++) {
        for (size_t j = 0; j < 3; j++) {
            array[i][j] = i + j + i * j;
        }
    }
    std::array<std::array<int32_t, 3>, 2> stdArray = array;
    EXPECT_2DARRAYEQ(array, stdArray, 2, 3);
    hidl_array<int32_t, 2, 3> array2 = stdArray;
    EXPECT_2DARRAYEQ(array, array2, 2, 3);
}
