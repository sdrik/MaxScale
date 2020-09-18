/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxbase/string.hh>
#include <maxscale/jansson.hh>
#include <maxtest/maxrest.hh>
#include <maxtest/testconnections.hh>

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("This is prototype of Columnstore test");

    return test.global_result;
}
