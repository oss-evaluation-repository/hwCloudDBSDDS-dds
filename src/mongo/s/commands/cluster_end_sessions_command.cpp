/**
 *    Copyright (C) 2017 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kCommand

#include "mongo/platform/basic.h"

#include "mongo/base/init.h"
#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/client.h"
#include "mongo/db/commands.h"
#include "mongo/db/logical_session_cache.h"
#include "mongo/db/logical_session_id_helpers.h"
#include "mongo/db/operation_context.h"
#include "mongo/s/commands/cluster_commands_helpers.h"
#include "mongo/util/log.h"

namespace mongo {

class ClusterEndSessionsCommand final : public ErrmsgCommandDeprecated {
    MONGO_DISALLOW_COPYING(ClusterEndSessionsCommand);

public:
    ClusterEndSessionsCommand() : ErrmsgCommandDeprecated("endSessions") {}

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kAlways;
    }
    bool adminOnly() const override {
        return false;
    }
    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }
    std::string help() const override {
        return "Cluster end a set of logical sessions";
    }
    Status checkAuthForOperation(OperationContext* opCtx,
                                 const std::string& dbname,
                                 const BSONObj& cmdObj) const override {
        // It is always ok to run this command, as long as you are authenticated
        // as some user, if auth is enabled.
        AuthorizationSession* authSession = AuthorizationSession::get(opCtx->getClient());
        try {
            auto user = authSession->getSingleUser();
            invariant(user);
            return Status::OK();
        } catch (...) {
            return exceptionToStatus();
        }
    }

    bool errmsgRun(OperationContext* opCtx,
                   const std::string& dbName,
                   const BSONObj& cmdObj,
                   std::string& errmsg,
                   BSONObjBuilder& output) override {
        // First: shard endSessions.
        const NamespaceString nss(CommandHelpers::parseNsCollectionRequired(dbName, cmdObj));

        LOG(1) << "EndSessionsCmd " << dbName << " " << nss;

        auto shardResponses = scatterGatherOnlyVersionIfUnsharded(
            opCtx,
            nss,
            CommandHelpers::filterCommandRequestForPassthrough(cmdObj),
            ReadPreferenceSetting::get(opCtx),
            Shard::RetryPolicy::kNoRetry);

        // TODO: the order of command return and mongos endSession
        // shard commands return error ??
        appendRawResponses(opCtx, &errmsg, &output, std::move(shardResponses));

        // Second: mongos endSessions.

        auto lsCache = LogicalSessionCache::get(opCtx);

        auto cmd = EndSessionsCmdFromClient::parse("EndSessionsCmdFromClient"_sd, cmdObj);

        lsCache->endSessions(makeLogicalSessionIds(cmd.getEndSessions(), opCtx));

        return true;
    }
} clusterEndSessionsCommand;

}  // namespace mongo
