let database;
const dbConfig = require('../config/database.js');

var isPgSql = true;
isPgSql = (dbConfig.hrPool.dbType=='POSTGRES');
database =  isPgSql ? require('../services/database_pg.js') : require('../services/database.js')

let baseQuery = 
 `select aalm_id "id",
    alarm_id "alarm_id",
    user_name "user_name",
    dataval "dataval",
    currentstate "currentstate",
    trigertime "trigertime",
    acktime "acktime",
    recovtime "recovtime",
    logtime "logtime",
    logdate "logdate",
    mailsent "mailsent",
    ackmessage "ackmessage"
  from `;

 baseQuery += isPgSql ? `logs.aalm_table` : `aalm_table`;

async function find(context) {
  let query = baseQuery;
  const binds = {};
  const values = [];

  if (context.id) {
    binds.aalm_id = context.id;
	values.push(context.id);
    query +=  isPgSql ? `\nwhere aalm_id = $1` : `\nwhere aalm_id = :aalm_id`;
  }

   let result = isPgSql ? (await database.query(query,values)) :  (await database.simpleExecute(query, binds));
  
  return result.rows;
}

module.exports.find = find;