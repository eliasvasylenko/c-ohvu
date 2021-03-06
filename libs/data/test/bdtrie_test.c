#include <stdbool.h>
#include <stdlib.h> 
#include <string.h>

#include <unity.h>

#include "c-ohvu/data/bdtrie.h"

static bdtrie trie;

typedef enum operation {
	INSERT,
	DELETE
} operation;

typedef struct update {
	char* key;
	uint32_t index;
	operation op;
} update;

int compare_entries(const void* a, const void* b) {
	const update* ua = a;
	const update* ub = b;
	int c = strcmp(ua->key, ub->key);
	if (!c) {
		c = ua->index - ub->index;
	}
	return c;
}

void* alloc_value(uint32_t key_size, const void* key_data, const void* value_data, bdtrie_node* owner) {
	uint32_t* v = malloc(sizeof(uint32_t));
	*v = *(uint32_t*)value_data;
	return v;
}

void update_value(void* value, bdtrie_node* owner) {
	;
}

void free_value(void* value) {
	free(value);
}

void setUp() {
	trie = (bdtrie){
		NULL,
		alloc_value,
		update_value,
		free_value
	};
}

void tearDown() {
	bdtrie_clear(&trie);
}

size_t sort_updates(size_t s, update* u) {
	qsort(u, s, sizeof(update), compare_entries);
	int count = 0, i = 0, j = 1;
	while (i < s) {
		if ((j == s || strcmp(u[i].key, u[j].key) != 0) && u[i].op == INSERT) {
			u[count++] = u[i];
		}
		i++; j++;
	}
	return count;
}

void apply_updates(size_t s, update* u) {
	for (int i = 0; i < s; i++) {
		switch (u[i].op) {
			case INSERT:
				;
				bdtrie_value v = bdtrie_insert(&trie, strlen(u[i].key), u[i].key, &u[i].index);
				break;
			case DELETE:
				bdtrie_delete(bdtrie_find(&trie, strlen(u[i].key), u[i].key).node);
				break;
		}
	}
}

void test_updates(size_t s, update* u) {
	apply_updates(s, u);
	s = sort_updates(s, u);

	int c = 0;
	for (bdtrie_value v = bdtrie_first(&trie); bdtrie_is_present(v); v = bdtrie_next(v)) {
		c++;
	}

	printf("   [ ");

	char** actual_keys = malloc(sizeof(char*) * c);
	int32_t* actual_values = malloc(sizeof(int32_t) * c);
	int i = 0;
	for (bdtrie_value v = bdtrie_first(&trie); bdtrie_is_present(v); v = bdtrie_next(v)) {
		uint32_t k = bdtrie_key_size(v.node);
		actual_keys[i] = malloc(sizeof(char) * k + 1);
		bdtrie_key(actual_keys[i], v.node);
		actual_keys[i][k] = '\0';

		printf("%s ", actual_keys[i]);

		actual_values[i] = *(uint32_t*)v.data;

		bdtrie_node* n = v.node;
		uint32_t actualSize = n->key_size;
		while (n->trie->root != n) {
			n = n->parent;
			actualSize += n->key_size;
		}

		TEST_ASSERT_EQUAL_INT32(k, actualSize);

		TEST_ASSERT_EQUAL_INT64(&trie, bdtrie_trie(v.node));
		i++;
	}

	printf("]\n = [ ");

	char** expected_keys = malloc(sizeof(char*) * s);
	int32_t* expected_values = malloc(sizeof(int32_t) * s);
	for (int i = 0; i < s; i++) {
		expected_keys[i] = u[i].key;
		expected_values[i] = u[i].index;

		printf("%s ", expected_keys[i]);
	}

	printf("]\n");

	TEST_ASSERT_EQUAL_INT32(s, c);
	if (s > 0 && c > 0) {
		uint32_t min = s < c ? s : c;
		TEST_ASSERT_EQUAL_STRING_ARRAY(expected_keys, actual_keys, min);
		TEST_ASSERT_EQUAL_INT32_ARRAY(expected_values, actual_values, min);
	}

	for (int i = 0; i < c; i++) {
		free(actual_keys[i]);
	}
	free(expected_keys);
	free(expected_values);
	free(actual_keys);
	free(actual_values);
}

void test_insert(size_t s, char** k) {
	update* u = malloc(sizeof(update) * s);
	for (int i = 0; i < s; i++) {
		u[i] = (update){ k[i], i, INSERT };
	}

	test_updates(s, u);

	free(u);
}

typedef struct test_update {
	char* key;
	operation op;
} test_update;

void test_insert_and_remove(size_t s, test_update* t) {
	update* u = malloc(sizeof(update) * s);
	for (int i = 0; i < s; i++) {
		u[i] = (update){ t[i].key, i, t[i].op };
	}

	test_updates(s, u);

	free(u);
}

void test_insert_1() {
	char* k[] = {};
	test_insert(sizeof(k) / sizeof(char*), k);
}

void test_insert_2() {
	char* k[] = { "" };
	test_insert(sizeof(k) / sizeof(char*), k);
}

void test_insert_3() {
	char* k[] = { "a" };
	test_insert(sizeof(k) / sizeof(char*), k);
}

void test_insert_4() {
	char* k[] = { "", "a", "ab" };
	test_insert(sizeof(k) / sizeof(char*), k);
}

void test_insert_5() {
	char* k[] = { "a", "", "ab" };
	test_insert(sizeof(k) / sizeof(char*), k);
}

void test_insert_6() {
	char* k[] = { "a", "a", "a", "ab" };
	test_insert(sizeof(k) / sizeof(char*), k);
}

void test_insert_7() {
	char* k[] = { "abcdef", "abc", "abcx" };
	test_insert(sizeof(k) / sizeof(char*), k);
}

void test_insert_8() {
	char* k[] = { "abc", "abd", "abz", "abe", "aby", "abx", "abz", "abzzz" };
	test_insert(sizeof(k) / sizeof(char*), k);
}

void test_insert_9() {
	char* k[] = { "aaz", "aay", "aax" };
	test_insert(sizeof(k) / sizeof(char*), k);
}

void test_insert_10() {
	char* k[] = { "aaz", "aay", "aax", "a" };
	test_insert(sizeof(k) / sizeof(char*), k);
}

void test_insert_11() {
	char* k[] = { "aaz", "aay", "aax", "ab" };
	test_insert(sizeof(k) / sizeof(char*), k);
}

void test_insert_12() {
	char* k[] = { "aaz", "aay", "aax", "aa" };
	test_insert(sizeof(k) / sizeof(char*), k);
}



void test_insert_and_remove_1() {
	test_update k[] = { { "a", INSERT }, { "a", DELETE } };
	test_insert_and_remove(sizeof(k) / sizeof(test_update), k);
}

void test_insert_and_remove_2() {
	test_update k[] = { { "a", INSERT }, { "aa", INSERT }, { "a", DELETE } };
	test_insert_and_remove(sizeof(k) / sizeof(test_update), k);
}

void test_insert_and_remove_3() {
	test_update k[] = { { "a", INSERT }, { "aa", INSERT }, { "aa", DELETE } };
	test_insert_and_remove(sizeof(k) / sizeof(test_update), k);
}

void test_insert_and_remove_4() {
	test_update k[] = { { "a", INSERT }, { "aa", INSERT }, { "ab", INSERT }, { "a", DELETE } };
	test_insert_and_remove(sizeof(k) / sizeof(test_update), k);
}

void test_insert_and_remove_5() {
	test_update k[] = {
		{ "a", INSERT },
		{ "aa", INSERT },
		{ "aaab", INSERT },
		{ "aaac", INSERT },
		{ "aa", DELETE },
		{ "aa", INSERT },
		{ "aa", DELETE },
		{ "aa", INSERT },
		{ "aa", DELETE }
	};
	test_insert_and_remove(sizeof(k) / sizeof(test_update), k);
}

void test_insert_and_remove_6() {
	test_update k[] = {
		{ "a", INSERT },
		{ "aa", INSERT },
		{ "ab", INSERT },
		{ "ac", INSERT },
		{ "ad", INSERT },
		{ "ad", DELETE },
		{ "ac", DELETE },
		{ "ab", DELETE },
		{ "aa", DELETE }
	};
	test_insert_and_remove(sizeof(k) / sizeof(test_update), k);
}

void test_insert_and_remove_7() {
	test_update k[] = {
		{ "a", INSERT },
		{ "aa", INSERT },
		{ "ab", INSERT },
		{ "ac", INSERT },
		{ "ad", INSERT },
		{ "aa", DELETE },
		{ "ab", DELETE },
		{ "ac", DELETE },
		{ "ad", DELETE }
	};
	test_insert_and_remove(sizeof(k) / sizeof(test_update), k);
}

void test_insert_and_remove_8() {
	/*
	 * TODO test update_value
	 */
	test_update k[] = {
		{ "data", INSERT },
		{ "system", INSERT },
		{ "text", INSERT },
		{ "reduce", INSERT },
		{ "fail", INSERT },
		{ "succeed", INSERT },
		{ "fa", INSERT },
		{ "reduce", DELETE },
		{ "fail", DELETE },
		{ "succeed", DELETE },
		{ "fa", DELETE },
		{ "data", DELETE },
		{ "system", DELETE },
		{ "text", DELETE }
	};
	test_insert_and_remove(sizeof(k) / sizeof(test_update), k);
}

void test_insert_and_remove_9() {
	test_update k[] = {
		{ "data", INSERT },
		{ "system", INSERT },
		{ "text", INSERT },
		{ "reduce", INSERT },
		{ "fail", INSERT },
		{ "succeed", INSERT },
		{ "fa", INSERT },
		{ "reduce", DELETE },
		{ "fail", DELETE }
	};
	test_insert_and_remove(sizeof(k) / sizeof(test_update), k);
}

void test_insert_and_remove_10() {
}

void test_insert_and_remove_11() {
}

void test_insert_and_remove_12() {
}

int main(void) {
	UNITY_BEGIN();

	RUN_TEST(test_insert_1);
	RUN_TEST(test_insert_2);
	RUN_TEST(test_insert_3);
	RUN_TEST(test_insert_4);
	RUN_TEST(test_insert_5);
	RUN_TEST(test_insert_6);
	RUN_TEST(test_insert_7);
	RUN_TEST(test_insert_8);
	RUN_TEST(test_insert_9);
	RUN_TEST(test_insert_10);
	RUN_TEST(test_insert_11);
	RUN_TEST(test_insert_12);

	RUN_TEST(test_insert_and_remove_1);
	RUN_TEST(test_insert_and_remove_2);
	RUN_TEST(test_insert_and_remove_3);
	RUN_TEST(test_insert_and_remove_4);
	RUN_TEST(test_insert_and_remove_5);
	RUN_TEST(test_insert_and_remove_6);
	RUN_TEST(test_insert_and_remove_7);
	RUN_TEST(test_insert_and_remove_8);
	RUN_TEST(test_insert_and_remove_9);
	RUN_TEST(test_insert_and_remove_10);
	RUN_TEST(test_insert_and_remove_11);
	RUN_TEST(test_insert_and_remove_12);

	return UNITY_END();
}

