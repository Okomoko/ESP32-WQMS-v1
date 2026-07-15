let database;
const dbConfig = require('../config/database.js');

var isPgSql = true;
isPgSql = (dbConfig.hrPool.dbType=='POSTGRES');
database =  isPgSql ? require('../services/database_pg.js') : require('../services/database.js')

const baseQuery = 
 `select kanal_id "id",
    kanal_name "channelname",
    mediatype_id "mediatype_id",
    sockettype_id "sockettype_id",
    paritytype_id "paritytype_id",
    protocoltype_id "protocoltype_id",
    serverport "serverport",
    listenport "listenport",
    serverip "serverip",
    filterip "filterip",
    comport "comport",
    baudrate "baudrate",
    databit "databit",
    stopbit "stopbit",
    responseto "responseto",
    connectionto "connectionto",
    frameto "frameto",
    groupid "groupid",
    station "station",
    enabled "enabled",
    logicpropath "logicpropath",
    description "description",
    modified "modified",
    state "state"
  from channels`;

async function find(context) {
  let query = baseQuery;
  const binds = {};
  const values = [];

  if (context.id) {
    binds.kanal_id = context.id;
	values.push(context.id);
    query += isPgSql ? `\nwhere kanal_id = $1` :`\nwhere kanal_id = :kanal_id`;
  }

   let result = isPgSql ? (await database.query(query,values)) :  (await database.simpleExecute(query, binds));


  return result.rows;
}

module.exports.find = find;