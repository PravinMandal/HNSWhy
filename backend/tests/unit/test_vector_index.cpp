#include "product/vector_index.h"

#include <array>
#include <vector>

#include "framework/test_framework.h"
#include "product/errors.h"

HNSWHY_TEST(vector_index_rebuild_add_search_and_tombstone) {
    hnswhy::VectorIndex index;
    index.rebuild({{10, {1.0f, 0.0f}},
                   {11, {0.0f, 1.0f}},
                   {12, {1.0f, 1.0f}},
                   {13, {-1.0f, 0.0f}}});
    CHECK_EQ(index.live(), std::size_t{4});
    CHECK_EQ(index.slot_count(), std::size_t{4});
    CHECK_EQ(index.dim(), std::size_t{2});
    index.validate();

    const std::array<float, 2> query{1.0f, 0.0f};
    auto hits = index.search(query, 1, 16);
    CHECK_EQ(hits.size(), std::size_t{1});
    CHECK_EQ(hits[0].chunk_id, hnswhy::Id{10});

    index.add({{14, {2.0f, 0.0f}}});
    index.validate();
    CHECK_EQ(index.live(), std::size_t{5});
    CHECK(index.slot_for_chunk(14).has_value());
    CHECK(index.vector_for_chunk(14).has_value());

    index.remove({10});
    index.validate();
    CHECK_EQ(index.live(), std::size_t{4});
    CHECK_EQ(index.slot_count(), std::size_t{5});
    hits = index.search(query, 1, 16);
    CHECK_EQ(hits.size(), std::size_t{1});
    CHECK(hits[0].chunk_id != 10);

    index.remove({10});
    CHECK_EQ(index.live(), std::size_t{4});  // idempotent

    index.rebuild({});
    CHECK_EQ(index.live(), std::size_t{0});
    CHECK_EQ(index.slot_count(), std::size_t{0});
    CHECK_EQ(index.dim(), std::size_t{0});
    CHECK(index.search(query, 1, 16).empty());
    index.validate();
}

HNSWHY_TEST(vector_index_rejects_bad_inputs) {
    hnswhy::VectorIndex index;
    CHECK_THROWS_AS(index.rebuild({{1, {1.0f}}, {2, {1.0f, 2.0f}}}), hnswhy::ProductError);
    CHECK_THROWS_AS(index.rebuild({{1, {1.0f}}, {1, {2.0f}}}), hnswhy::ProductError);
    CHECK_THROWS_AS(index.rebuild({{1, {}}}), hnswhy::ProductError);

    index.rebuild({{1, {1.0f, 0.0f}}});
    CHECK_THROWS_AS(index.add({{2, {1.0f, 0.0f, 0.0f}}}), hnswhy::ProductError);
    CHECK_THROWS_AS(index.add({{1, {0.0f, 1.0f}}}), hnswhy::ProductError);
    const std::array<float, 3> wrong_query{1.0f, 0.0f, 0.0f};
    CHECK_THROWS_AS(index.search(wrong_query, 1, 16), hnswhy::ProductError);
}

HNSWHY_TEST(vector_index_rejects_a_duplicate_mid_batch_without_mutating) {
    hnswhy::VectorIndex index;
    index.rebuild({{1, {1.0f, 0.0f}}});

    // The duplicate is the second record: validation must run over the whole
    // batch before the first record reaches the store or graph.
    CHECK_THROWS_AS(index.add({{2, {0.0f, 1.0f}}, {2, {1.0f, 1.0f}}}),
                    hnswhy::ProductError);
    CHECK_EQ(index.live(), std::size_t{1});
    CHECK_EQ(index.slot_count(), std::size_t{1});
    CHECK(!index.slot_for_chunk(2).has_value());

    // A collision with an existing chunk id must also leave the batch untouched.
    CHECK_THROWS_AS(index.add({{3, {0.0f, 1.0f}}, {1, {1.0f, 1.0f}}}),
                    hnswhy::ProductError);
    CHECK_EQ(index.live(), std::size_t{1});
    CHECK(!index.slot_for_chunk(3).has_value());
    index.validate();
}

HNSWHY_TEST(vector_index_maps_after_deletion_and_rebuild) {
    hnswhy::VectorIndex index;
    index.rebuild({{5, {1.0f, 0.0f}}, {6, {0.0f, 1.0f}}});
    index.remove({5});
    CHECK_EQ(index.vector_for_chunk(5)->size(), std::size_t{2});  // tombstone vector remains
    CHECK(!index.slot_for_chunk(99).has_value());

    index.rebuild({{7, {0.0f, 1.0f}}});
    CHECK(!index.slot_for_chunk(5).has_value());
    CHECK(index.slot_for_chunk(7).has_value());
    index.validate();
}
