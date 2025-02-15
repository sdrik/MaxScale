/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// https://docs.mongodb.com/manual/reference/command/buildInfo/

const assert = require('assert');
const test = require('./nosqltest')

const name = "buildInfo";

describe(name, function () {
    this.timeout(test.timeout);

    let mng;
    let mxs;

    function check_standard_fields(doc) {
        assert.notEqual(doc.gitVersion, undefined);
        assert.notEqual(doc.versionArray, undefined);
        assert.notEqual(doc.version, undefined);
        assert.notEqual(doc.storageEngines, undefined);
        assert.notEqual(doc.javascriptEngine, undefined);
        assert.notEqual(doc.bits, undefined);
        assert.notEqual(doc.debug, undefined);
        assert.notEqual(doc.maxBsonObjectSize, undefined);
        assert.notEqual(doc.openssl, undefined);
        assert.notEqual(doc.modules, undefined);
    }

    function compare_field_types(doc1, doc2) {
        for (var f in doc1) {
            assert.equal(typeof doc1[f], typeof doc2[f]);
        }
    }

    /*
     * MOCHA
     */
    before(async function () {
        mng = await test.MDB.create(test.MngMongo, "admin");
        mxs = await test.MDB.create(test.MxsMongo, "admin");
    });

    it('Returns the expected fields.', async function () {
        var rv1 = await mng.runCommand({buildInfo: 1});
        var rv2 = await mxs.runCommand({buildInfo: 1});

        check_standard_fields(rv1);
        check_standard_fields(rv2);

        delete rv2.maxscale; // Won't be found on the one returned by Mongo.

        compare_field_types(rv2, rv1);
    });

    after(async function () {
        if (mxs) {
            await mxs.close();
        }

        if (mng) {
            await mng.close();
        }
    });
});
