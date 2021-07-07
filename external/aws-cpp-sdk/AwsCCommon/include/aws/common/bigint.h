#ifndef AWS_COMMON_BIGINT_H
#define AWS_COMMON_BIGINT_H

/*
 * Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include <aws/common/common.h>

#include <aws/common/array_list.h>
#include <aws/common/byte_buf.h>

struct aws_bigint;

AWS_EXTERN_C_BEGIN

AWS_COMMON_API
bool aws_bigint_is_valid(const struct aws_bigint *bigint);

AWS_COMMON_API
void aws_bigint_destroy(struct aws_bigint *bigint);

/**
 * Creates a big int from a string of hex digits.  String may start with "0x".  Leading zeros are skipped.
 * An empty string is considered an error.  A leading (-) symbol is not supported.  Use aws_bigint_negate() after
 * calling aws_bigint_new_from_hex() to generate an arbitrary negative number.
 */
AWS_COMMON_API
struct aws_bigint *aws_bigint_new_from_hex(struct aws_allocator *allocator, struct aws_byte_cursor hex_digits);

/**
 * Creates a big int from a 64 bit signed integer
 */
AWS_COMMON_API
struct aws_bigint *aws_bigint_new_from_int64(struct aws_allocator *allocator, int64_t value);

/**
 * Creates a big int from a 64 bit unsigned integer
 */
AWS_COMMON_API
struct aws_bigint *aws_bigint_new_from_uint64(struct aws_allocator *allocator, uint64_t value);

/**
 * Creates a big int as a copy of another big int
 */
AWS_COMMON_API
struct aws_bigint *aws_bigint_new_from_copy(const struct aws_bigint *source);

/**
 * Creates a big int from a sequence of bytes
 */
AWS_COMMON_API
struct aws_bigint *aws_bigint_new_from_cursor(struct aws_allocator *allocator, struct aws_byte_cursor source);

/**
 * Writes a bigint to a buffer as a hexadecimal number.  Will prepend (-) in front of negative numbers for
 * easier testing.  This API is primarily intended for testing.  Actual output (to various formats/bases) is TBD.
 */
AWS_COMMON_API
int aws_bigint_bytebuf_debug_output(const struct aws_bigint *bigint, struct aws_byte_buf *buffer);

/**
 * Writes a bigint to a buffer as a big endian sequence of octets.
 *
 * If minimum_length is non-zero, then leading zero-bytes will pad the output as necessary.  Otherwise only the minimum
 * number of bytes will be written.
 */
AWS_COMMON_API
int aws_bigint_bytebuf_append_as_big_endian(
    const struct aws_bigint *bigint,
    struct aws_byte_buf *buffer,
    size_t minimum_length);

/**
 * Returns true if this integer is negative, false otherwise.
 */
AWS_COMMON_API
bool aws_bigint_is_negative(const struct aws_bigint *bigint);

/**
 * Returns true if this integer is positive, false otherwise.
 */
AWS_COMMON_API
bool aws_bigint_is_positive(const struct aws_bigint *bigint);

/**
 * Returns true if this integer is zero, false otherwise.
 */
AWS_COMMON_API
bool aws_bigint_is_zero(const struct aws_bigint *bigint);

/**
 * Returns true if the two big ints are equal in value, false otherwise
 */
AWS_COMMON_API
bool aws_bigint_equals(const struct aws_bigint *lhs, const struct aws_bigint *rhs);

/**
 * Returns true if the two big ints are not equal in value, false otherwise
 */
AWS_COMMON_API
bool aws_bigint_not_equals(const struct aws_bigint *lhs, const struct aws_bigint *rhs);

/**
 * Returns true if the first operand is less than the second operand
 */
AWS_COMMON_API
bool aws_bigint_less_than(const struct aws_bigint *lhs, const struct aws_bigint *rhs);

/**
 * Returns true if the first operand is less than or equal to the second operand
 */
AWS_COMMON_API
bool aws_bigint_less_than_or_equals(const struct aws_bigint *lhs, const struct aws_bigint *rhs);

/**
 * Returns true if the first operand is greater than the second operand
 */
AWS_COMMON_API
bool aws_bigint_greater_than(const struct aws_bigint *lhs, const struct aws_bigint *rhs);

/**
 * Returns true if the first operand is greater than or equal to the second operand
 */
AWS_COMMON_API
bool aws_bigint_greater_than_or_equals(const struct aws_bigint *lhs, const struct aws_bigint *rhs);

/**
 * Negates the supplied bigint.  Has no effect on a zero-valued integer.
 */
AWS_COMMON_API
void aws_bigint_negate(struct aws_bigint *bigint);

/*
 * Adds two big integers, placing the result in output.  Output must have been initialized first.  Output
 * may alias to either operand.
 */
AWS_COMMON_API
int aws_bigint_add(struct aws_bigint *output, const struct aws_bigint *lhs, const struct aws_bigint *rhs);

/*
 * Subtracts two big integers, placing the result in output.  Output must have been initialized first.  Output
 * may alias to either operand (aliasing to the second is weird but not forbidden).
 */
AWS_COMMON_API
int aws_bigint_subtract(struct aws_bigint *output, const struct aws_bigint *lhs, const struct aws_bigint *rhs);

/*
 * Multiplies two big integers, placing the result in output.  Output must have been initialized first.  Output
 * may alias to either operand.
 */
AWS_COMMON_API
int aws_bigint_multiply(struct aws_bigint *output, const struct aws_bigint *lhs, const struct aws_bigint *rhs);

/*
 * Performs a right bit-shift on a big int, equivalently dividing by a power of two.
 */
AWS_COMMON_API
void aws_bigint_shift_right(struct aws_bigint *bigint, size_t shift_amount);

/*
 * Performs a left bit-shift on a big int, equivalently multiplying by a power of two.
 */
AWS_COMMON_API
int aws_bigint_shift_left(struct aws_bigint *bigint, size_t shift_amount);

/*
 * Divides two *non-negative* big integers, computing both the quotient and the remainder.  Quotient and remainder
 * must already be initialized.  Quotient and remainder may alias to operands but not to each other.
 */
AWS_COMMON_API
int aws_bigint_divide(
    struct aws_bigint *quotient,
    struct aws_bigint *remainder,
    const struct aws_bigint *lhs,
    const struct aws_bigint *rhs);

AWS_EXTERN_C_END

#endif /* AWS_COMMON_BIGINT_H */
