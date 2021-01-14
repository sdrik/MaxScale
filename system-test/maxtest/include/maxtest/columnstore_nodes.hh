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

    Columnstore_nodes(SharedData& shared, const std::string& network_config)
        : Mariadb_nodes("columnstore", shared, network_config, Type::COLUMNSTORE)
    {
    }

    //virtual int start_replication();
    virtual int check_replication();
    virtual int fix_replication();
    /**
     * Get the configuration file name for a particular node
     *
     * @param node Node number for which the configuration is requested
     *
     * @return The name of the configuration file
     */
    virtual std::string get_config_name(int node);

};
