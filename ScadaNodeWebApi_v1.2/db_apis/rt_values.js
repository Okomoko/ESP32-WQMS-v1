let database;
const dbConfig = require('../config/database.js');

var isPgSql = true;
isPgSql = (dbConfig.hrPool.dbType=='POSTGRES');
database =  isPgSql ? require('../services/database_pg.js') : require('../services/database.js')

let baseQuery = 
 `select lct_id "id",
    tag_id "tag_id",
    dataval "dataval",
    logtime "logtime",
    logdate "logdate"
  from ` ;
  
  baseQuery += isPgSql ? `logs.lct_table` : `lct_table`;

async function find(context) {
  let query = baseQuery;
  const binds = {};
  const values = [];

  if (context.id) {
    binds.lct_id = context.id;
	values.push(context.id);
    query += isPgSql ? `\nwhere lct_id = $1` :`\nwhere lct_id = :lct_id`;
  }

   let result = isPgSql ? (await database.query(query,values)) :  (await database.simpleExecute(query, binds));


  return result.rows;
}

module.exports.find = find;