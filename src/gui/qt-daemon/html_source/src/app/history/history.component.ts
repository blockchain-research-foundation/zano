import {Component, OnInit, OnDestroy, AfterViewChecked, ViewChild, ElementRef} from '@angular/core';
import {VariablesService} from '../_helpers/services/variables.service';

@Component({
  selector: 'app-history',
  templateUrl: './history.component.html',
  styleUrls: ['./history.component.scss']
})
export class HistoryComponent implements OnInit, OnDestroy, AfterViewChecked {

  openedDetails = false;
  calculatedWidth = [];
  @ViewChild('head') head: ElementRef;

  constructor(private variablesService: VariablesService) {}

  ngOnInit() {}

  ngAfterViewChecked() {
    this.calculateWidth();
  }

  getHeight(item) {
    if ((this.variablesService.height_app - item.height >= 10 && item.height !== 0) || (item.is_mining === true && item.height === 0)) {
      return 100;
    } else {
      if (item.height === 0 || this.variablesService.height_app - item.height < 0) {
        return 0;
      } else {
        return (this.variablesService.height_app - item.height) * 10;
      }
    }
  }

  openDetails(index) {
    if (index === this.openedDetails) {
      this.openedDetails = false;
    } else {
      this.openedDetails = index;
    }
  }

  calculateWidth() {
    this.calculatedWidth = [];
    this.calculatedWidth.push(this.head.nativeElement.childNodes[0].clientWidth);
    this.calculatedWidth.push(this.head.nativeElement.childNodes[1].clientWidth + this.head.nativeElement.childNodes[2].clientWidth);
    this.calculatedWidth.push(this.head.nativeElement.childNodes[3].clientWidth);
    this.calculatedWidth.push(this.head.nativeElement.childNodes[4].clientWidth);
  }

  ngOnDestroy() {}

}
