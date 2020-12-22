/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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
#pragma once

/**
 * @file columnstorex_nodes.h - work with Columnstor setup
 *
 */

#include <cerrno>
#include <string>
#include <maxtest/mariadb_nodes.hh>
#include <maxtest/nodes.hh>
#include <maxtest/mariadb_func.hh>

#define CLUSTRIX_DEPS_YUM "yum install -y bzip2 wget screen ntp ntpdate vim htop mdadm"
#define WGET_CLUSTRIX     "wget http://files.clustrix.com/releases/software/clustrix-9.1.4.el7.tar.bz2"
#define UNPACK_CLUSTRIX   "tar xvjf clustrix-9.1.4.el7.tar.bz2"
#define INSTALL_CLUSTRIX  "cd clustrix-9.1.4.el7; sudo ./clxnode_install.py --yes --force"

class Columnstore_nodes : public Mariadb_nodes
{
public:

    Columnstore_nodes(const char *pref, const char *test_cwd, bool verbose, const std::string& network_config):
        Mariadb_nodes(pref, test_cwd, verbose, network_config) { }

    virtual int start_replication();
    virtual int check_replication();
    virtual int fix_replication();

};
