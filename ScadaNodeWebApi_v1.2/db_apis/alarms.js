let database;
const dbConfig = require('../config/database.js');

var isPgSql = true;
isPgSql = (dbConfig.hrPool.dbType=='POSTGRES');
database =  isPgSql ? require('../services/database_pg.js') : require('../services/database.js')


const baseQuery = 
 `select alarm_id "id",
    alarm_name "alarm_name",
    alarm_desc "alarm_desc",
    alarm_class "alarm_class",
    enabled "enabled",
    ackrequired "ackrequired",
    islogging "islogging",
    tag_id "tag_id",
    alarm_type "alarm_type",
    alarm_level "alarm_level",
    comp_type "comp_type"
  from alarms`;

async function find(context) {
  let query = baseQuery;
  const binds = {};
  const values = [];

  if (context.id) {
    binds.alarm_id = context.id;

    query += isPgSql ? `\nwhere alarm_id = $1` : `\nwhere alarm_id = :alarm_id`;

  }

   let result = isPgSql ? (await database.query(query,values)) :  (await database.simpleExecute(query, binds));

  return result.rows;
}

module.exports.find = find;