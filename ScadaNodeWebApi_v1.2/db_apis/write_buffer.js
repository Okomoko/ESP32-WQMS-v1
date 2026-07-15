let database;
const dbConfig = require('../config/database.js');

var isPgSql = true;
isPgSql = (dbConfig.hrPool.dbType=='POSTGRES');
database =  isPgSql ? require('../services/database_pg.js') : require('../services/database.js')

let createSql =
`insert into ` ;

 createSql += isPgSql ? `logs.api_write_buffer` : `api_write_buffer`;
 createSql += `(
    tag_id,
    data_value,
    username,
    logtime
  ) values (`;

createSql +=  isPgSql ? `$1,$2,$3,$4)` : `:tag_id,:data_value,:username,:logtime)`;

async function writeValue(tagbuffer) {
  let query = createSql;
  const binds = {};
  const values = [];

  //console.log(tagbuffer);

  binds.tag_id = tagbuffer.body.tag_id;
  binds.data_value = tagbuffer.body.data_value;
  binds.username = tagbuffer.body.username;
  binds.logtime = tagbuffer.body.logtime;
  values.push(tagbuffer.body.tag_id);
  values.push(tagbuffer.body.data_value);
  values.push(tagbuffer.body.username);
  values.push(tagbuffer.body.logtime);

  //console.log(binds);
  //console.log(query);

   let result = isPgSql ? (await database.query(query,values)) :  (await database.simpleExecute(query, binds));
  
   return result.rowCount;
}

module.exports.writeValue = writeValue;