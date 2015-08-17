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

'use strict';

var clarinet = require('clarinet');
var fs = require('fs');
var stream = require('clarinet').createStream();
var outputStream = fs.createWriteStream('output');
var chalk = require('chalk');

var firstTime = true;
var finished = false;
var startTime;
var totalTime;

var in_event = false;
var events = 0;
var currentKey = '';
var currentEvent = {};

stream.on('error', function (e) {
  console.error(e)
  this._parser.error = null;
  this._parser.resume();
});

stream.on('key', function (key) {
  if (key === 'event') {
    in_event = true;
    return;
  }

  if (in_event) {
    currentKey = key;
  }
});

stream.on('openobject', function (key) {
  if (firstTime) {
    startTime = Date.now();
    firstTime = false;
  }
  if (in_event) {
    currentKey = key;
  }
});

stream.on('closeobject', function (key) {
  if (in_event) {
    in_event = false;
    events++;
    outputStream.write(JSON.stringify(currentEvent) + '\n');
  }
});

stream.on('value', function (value) {
  if (in_event) {
    currentEvent[currentKey] = value;
  }
});

stream.on('end', function () {
  totalTime = (Date.now() - startTime) / 1000;
  process.stdout.clearLine();
  console.log(chalk.green.bold('Ejecución finalizada'));
  console.log(chalk.yellow('Total de eventos procesados:\t') +
    chalk.bold(events));
  console.log(chalk.yellow('Tiempo de ejecución:\t\t') +
    chalk.bold(totalTime + 's'));
  console.log(chalk.yellow('Media de eventos por segundo:\t') +
    chalk.bold(Math.round(events / totalTime)));
  process.exit(0);
});

fs.createReadStream('input.json')
  .pipe(stream);

setInterval(function () {
  process.stdout.write(chalk.green('Procesado ' + events + ' eventos') +
    ' | ' +
    chalk.yellow(Math.round(events / ((Date.now() - startTime) / 1000)) +
      ' eventos/s\r'));
}, 250);
