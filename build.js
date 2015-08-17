/**
 ** Copyright (C) 2015 Eneo Tecnologia S.L.
 ** Author: Diego Fernández <bigomby@gmail.com>
 **
 ** This program is free software; you can redistribute it and/or modify
 ** it under the terms of the GNU Affero General Public License as
 ** published by the Free Software Foundation, either version 3 of the
 ** License, or (at your option) any later version.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU Affero General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/

var nexe = require('nexe');

nexe.compile({
  input: './rb_get_raw.js',
  output: './rb_get_raw',
  nodeVersion: '0.10.40',
  nodeTempDir: './tmp/nexe',
  python: 'python',
  flags: false,
  framework: "nodejs"
}, function (err) {
  console.log(err);
});
