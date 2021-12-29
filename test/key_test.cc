#include <gtest/gtest.h>
#include "../src/data_structures/key.h"

/// Note that these will have a max value of 255.
using EightBitKey = GenericKey<2, 8>;

/// Simple test to add one key to another, with key1 + key2 being less than
/// the total number keys in the ring.
/// Sum should be key 1's value + key 2's value.
TEST(KeyOpTest, AdditionNoModulo)
{
    EightBitKey key1(16), key2(15);
    EXPECT_TRUE(key1 + key2 == EightBitKey(31));
}

/// Addition test in which two keys have a sum exceeding the number chords in
/// the ring, causing their sum to "loop" around in the ring.
/// Sum should be key 1's value + key 2's value mod number keys in ring (256).
TEST(KeyOpTest, AdditionWithModulo)
{
    EightBitKey key1(128), key2(128);
    EightBitKey key3 = key2 + key1;
    EXPECT_TRUE(key1 + key2 == EightBitKey(0));
}

/// Subtraction test in which two keys have a non-negative difference.
/// Result should just be key 1's value - key 2's value.
TEST(KeyOpTest, SubstractionNoModulo)
{
     EightBitKey key1(16), key2(15);
     EXPECT_TRUE(key1 - key2 == EightBitKey(1));
}

/// Subtraction test in which two keys have a negative difference.
/// Result should be number keys in ring (256) plus key 1's val - key 2's val.
TEST(KeyOpTest, SubstractionWithModulo)
{
    EightBitKey key1(0), key2(1);
    EXPECT_TRUE(key1 - key2 == EightBitKey(255));
}

/// Clockwise in between test with exclusive boundaries and no modulo needed.
/// (i.e. LB < UB).
TEST(KeyInBetweenTest, ExclusiveNoModulo)
{
    ChordKey key1(75), key2(99);
    EXPECT_TRUE(key1.InBetween(0, 99, false));
    EXPECT_FALSE(key2.InBetween(0, 99, false));
}

/// Clockwise in between test with exclusive boundaries and modulo needed.
TEST(KeyInBetweenTest, ExclusiveWithModulo)
{
    ChordKey key1(1), key2(25);
    EXPECT_TRUE(key1.InBetween(75, 25, false));
    EXPECT_FALSE(key2.InBetween(75, 25, false));
}

/// Clockwise in between test with inclusive boundaries and no modulo needed.
TEST(KeyInBetweenTest, InclusiveNoModulo)
{
    ChordKey key1(75), key2(99);
    EXPECT_TRUE(key1.InBetween(0, 99, true));
    EXPECT_TRUE(key2.InBetween(0, 99, true));
}

/// Clockwise in between test with inclusive boundaries and modulo needed.
TEST(KeyInBetweenTest, InclusiveWithModulo)
{
    ChordKey key1(1), key2(25);
    EXPECT_TRUE(key1.InBetween(75, 25, true));
    EXPECT_TRUE(key2.InBetween(75, 25, true));
}

/// Clockwise in between test with keys of differing lengths (caused issues
/// in previous implementations when computing the size of the logical ring).
TEST(KeyInBetweenTest, DifferingLengths)
{
	// This was previously an edge case. The differing lengths of the keys
	// produced an inaccurate value for hex codes, so now we simply assume
	// a constant keyspace of 16^32 keys.
    ChordKey key("f4ee136cb4059b2883450e7e93698be", true),
        lb("633bd46b5c515992a5ce553d0680bec9", true),
        ub("f4ee136cb4059b2883450e7e93698bd", true);

	EXPECT_FALSE(key.InBetween(lb, ub, true));
}